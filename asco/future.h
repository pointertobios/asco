// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <functional>
#include <memory>
#include <memory_resource>
#include <type_traits>

#include <asco/concurrency/concurrency.h>
#include <asco/core/runtime.h>
#include <asco/core/worker.h>
#include <asco/panic/panic.h>
#include <asco/panic/unwind.h>
#include <asco/utils/concepts.h>
#include <asco/utils/erased.h>
#include <asco/utils/memory_slot.h>
#include <asco/utils/types.h>

namespace asco::base {

using namespace types;
using namespace concepts;

using core::task_id;

struct promise_type {};

template<move_secure T, bool Spawn, bool Core, bool UseReturnValue = true, typename StateExtra = void>
    requires(!(!Spawn && Core))  // Non-spawn coroutine controls by the caller or `co_await`er.
struct future_base {
    struct promise_type;

    using deliver_type = T;

protected:
    using coroutine_handle_type = std::coroutine_handle<promise_type>;

private:
    constexpr static bool deliver_type_is_void = std::is_void_v<deliver_type>;

    using task_type = core::task<T, Spawn, Core, UseReturnValue, StateExtra>;

    struct promise_base {
        task_id this_id;
        std::shared_ptr<task_type> this_task;
    };

    struct promise_void_mixin : promise_base {
        void return_void() noexcept {
            if (auto _ = this->this_task->sync_waiting_lock.read())
                this->this_task->returned.store(true, morder::release);

            if (this->this_task->wait_sem) {
                this->this_task->wait_sem->release();
            }
        }
    };

    struct promise_value_mixin : promise_base {
        void return_value(passing<monostate_if_void<deliver_type>> val) {
            this->this_task->delivery_slot.put(std::forward<deliver_type>(val));
            if (auto _ = this->this_task->sync_waiting_lock.read())
                this->this_task->returned.store(true, morder::release);

            if (this->this_task->wait_sem) {
                this->this_task->wait_sem->release();
            }
        }
    };

public:
    struct promise_type
            : std::conditional_t<
                  deliver_type_is_void || !UseReturnValue, promise_void_mixin, promise_value_mixin> {
        future_base get_return_object(
            panic::coroutine_trace_handle caller_cthdl =
                *cpptrace::stacktrace::current(1, 1).begin()) noexcept {
            auto id = core::runtime::this_runtime().alloc_task_id();
            promise_base::this_id = id;
            auto task = std::allocate_shared<task_type>(
                core::mm::allocator<core::task<T, Spawn, Core, UseReturnValue, StateExtra>>(
                    core::mm::default_pool<core::task<>>()),  // All tasks use the same pool to allocate
                id, coroutine_handle_type::from_promise(*this), caller_cthdl);

            auto &runtime = core::runtime::this_runtime();
            runtime.register_task(id, task);
            promise_base::this_task = task;

            if constexpr (Spawn) {
                if constexpr (Core) {
                    runtime.spawn_core_task(id, task);
                } else {
                    runtime.spawn_task(id, task);
                }
            } else if (core::worker::in_worker()) {
                // Non-spawn coroutine controls by the caller or `co_await`er.
                // So this coroutine's worker inherits from the caller.
                task->worker_ptr = &core::worker::this_worker();
                task->scheduled.store(true, morder::release);
                core::worker::this_worker().register_task(id, task, true);
            }

            return {id, std::move(task)};
        }

        auto initial_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() noexcept {
            this->this_task->e_ptr = std::current_exception();
            this->this_task->e_thrown.store(true, morder::release);

            if (this->this_task->wait_sem) {
                this->this_task->wait_sem->release();
            }
        }

        auto final_suspend() noexcept {
            if (!this->this_task->returned.load(morder::acquire)
                && !this->this_task->e_thrown.load(morder::acquire)) {
                if constexpr (deliver_type_is_void || !UseReturnValue) {
                    promise_void_mixin::return_void();
                } else {
                    panic::panic(
                        "[ASCO] future_base::promise_type::final_suspend: Coroutine ended without "
                        "returning a value or throwing an exception.");
                }
            }

            auto &worker = core::worker::this_worker();
            worker.suspend_task(promise_base::this_id);

            if constexpr (Spawn) {
                // We don't wait for `been_started_await` here.
                // Async functions can be never awaited, so we just skip resuming the caller task in that
                // case.
                // And also, we have called return_void(), return_value() or unhandled_exception() now,
                // the caller of this coroutine will never sleep and can't be awaken.
                if (promise_base::this_task->await_started.load(
                        morder::acquire)                // Has been `co_await`ed or .await()
                    && promise_base::this_task->caller  // Actually `co_await`ed
                    && promise_base::this_task->caller->scheduled.load(
                        morder::acquire)  // `worker_ptr` has been released
                ) {
                    promise_base::this_task->caller->worker_ptr->activate_task(
                        promise_base::this_task->caller->id);
                }
                return std::suspend_always{};
            } else {
                struct final_awaitable {
                    std::shared_ptr<core::task<>> caller_task;

                    bool await_ready() const noexcept { return false; }

                    auto await_suspend(std::coroutine_handle<> corohandle) const noexcept {
                        auto reschdl = caller_task->corohandle;
                        corohandle.destroy();  // Destroy here to avoid use-after-free
                        return reschdl;
                    }

                    void await_resume() const noexcept {}
                };
                core::runtime::this_runtime().unregister_task(promise_base::this_id);
                worker.task_exit();
                worker.unregister_task(promise_base::this_id);
                worker.activate_task(promise_base::this_task->caller->id);
                // If destroy coroutine handle here, we do use-after-free
                return final_awaitable{promise_base::this_task->caller};
            }
        }

#ifdef USING_LIBCXX
        void *operator new(size_t n) { return _allocator.allocate(n); }

        void operator delete(void *_ptr, size_t n) { _allocator.deallocate(_ptr, n); }

    private:
        static std::pmr::synchronized_pool_resource &_allocator;
#endif
    };

    bool await_ready() const noexcept {
        this_task->caller_coroutine_trace = *cpptrace::stacktrace::current(1, 1).begin();

        if constexpr (Spawn)
            return this_task->returned.load(morder::acquire) || this_task->e_thrown.load(morder::acquire);
        else
            return false;
    }

    bool sync_await_ready() const noexcept { return await_ready(); }

    bool await_ready_or_register_await_sem() noexcept
        requires(Spawn)
    {
        auto _ = this_task->sync_waiting_lock.write();
        if (sync_await_ready())
            return true;

        this_task->wait_sem.emplace(0);
        return false;
    }

    auto await_suspend(std::coroutine_handle<> _caller) noexcept {
        auto caller = core::runtime::this_runtime().get_task_by(_caller);
        this_task->caller = caller;
        this_task->await_started.store(true, morder::release);

        auto &worker = core::worker::this_worker();
        if constexpr (Spawn) {
            if (!await_ready()) {
                worker.suspend_task(caller->id);
                return true;
            }
            return false;
        } else {
            worker.suspend_task(caller->id);
            worker.activate_task(this_id);
            worker.task_enter(this_id);
            return this_task->corohandle;
        }
    }

    deliver_type await_resume() {
        (void)this_task->returned.load(morder::acquire);  // Ensure memory order

        if (this_task->e_thrown.load(morder::acquire) && !this_task->e_rethrown.load(morder::acquire)) {
            auto e = this_task->e_ptr;
            this_task->e_rethrown.store(true, morder::release);
            std::rethrow_exception(e);
        }

        if constexpr (!deliver_type_is_void)
            return this_task->delivery_slot.move();
    }

    deliver_type await()
        requires(Spawn)
    {
        if (core::worker::in_worker())
            panic::panic(
                "[ASCO] future_base::await(): Cannot sync await in async runtime, just use co_await.");

        this_task->raw_stacktrace = cpptrace::stacktrace::current(1);

        if (!await_ready_or_register_await_sem()) {
            this_task->await_started.store(true, morder::release);
            this_task->wait_sem->acquire();
        }

        if (this_task->e_thrown.load(morder::acquire) && !this_task->e_rethrown.load(morder::acquire)) {
            auto e = this_task->e_ptr;
            this_task->e_rethrown.store(true, morder::release);
            std::rethrow_exception(e);
        }

        if constexpr (!deliver_type_is_void)
            return this_task->delivery_slot.move();
    }

    future_base<deliver_type, true, false, UseReturnValue, StateExtra> spawn(this future_base self)
        requires(!Spawn)
    {
        if (self.this_task->scheduled.load(morder::acquire))
            self.this_task->worker_ptr->move_out_suspended_task(self.this_id);
        core::worker::this_worker().move_in_suspended_task(self.this_id, self.this_task);
        if constexpr (deliver_type_is_void)
            co_await self;
        else
            co_return co_await self;
    }

    future_base<deliver_type, true, true, UseReturnValue, StateExtra> spawn_core(this future_base self)
        requires(!Spawn)
    {
        if (self.this_task->scheduled.load(morder::acquire))
            self.this_task->worker_ptr->move_out_suspended_task(self.this_id);
        core::worker::this_worker().move_in_suspended_task(self.this_id, self.this_task);
        if constexpr (deliver_type_is_void)
            co_await self;
        else
            co_return co_await self;
    }

    future_base<void, true, false> ignore(
        this future_base self,
        std::function<void(std::exception_ptr)> on_exception = nullptr) noexcept {
        try {
            co_await self;
        } catch (...) {
            if (on_exception)
                on_exception(std::current_exception());
        }
    }

    void bind_lambda(std::unique_ptr<utils::erased> &&f) { this_task->bind_lambda(std::move(f)); }

    future_base() noexcept = default;

    future_base(task_id id, std::shared_ptr<task_type> &&task) noexcept
            : none{false}
            , this_id{id}
            , this_task{std::move(task)} {}

    future_base(const future_base &) = delete;
    future_base &operator=(const future_base &) = delete;

    future_base(future_base &&other) noexcept
            : none{other.none}
            , this_id{other.this_id}
            , this_task{std::move(other.this_task)} {
        other.none = true;
    }

    future_base &operator=(future_base &&other) noexcept {
        if (this != &other) {
            this->~future_base();
            none = other.none;
            this_id = other.this_id;
            this_task = std::move(other.this_task);
            other.none = true;
        }
        return *this;
    }

    ~future_base() noexcept = default;

protected:
    bool none{true};

    task_id this_id{0};
    std::shared_ptr<task_type> this_task{nullptr};
};

#ifdef USING_LIBCXX

template<move_secure T, bool Spawn, bool Core, bool UseReturnValue, typename StateExtra>
    requires(!(!Spawn && Core))
inline std::pmr::synchronized_pool_resource
    &future_base<T, Spawn, Core, UseReturnValue, StateExtra>::promise_type::_allocator{
        core::mm::default_pool<base::promise_type>()};

#endif

};  // namespace asco::base

namespace asco {

template<concepts::move_secure T>
using future = base::future_base<T, false, false>;

template<concepts::move_secure T>
using future_spawn = base::future_base<T, true, false>;

template<concepts::move_secure T>
using future_core = base::future_base<T, true, true>;

using runtime_initializer_t = std::optional<std::function<core::runtime_builder()>>;

namespace concepts {

template<typename Fn>
struct is_specialization_of_future_type {
private:
    template<move_secure T, bool Spawn, bool Core, bool UseReturnValue, typename StateExtra>
    static std::true_type test(base::future_base<T, Spawn, Core, UseReturnValue, StateExtra> *);

    template<typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test(std::declval<Fn *>()))::value;
};

template<typename T>
constexpr bool is_specialization_of_future_type_v = is_specialization_of_future_type<T>::value;

template<typename F>
concept future_type = is_specialization_of_future_type_v<std::remove_cvref_t<F>>;

template<typename Fn, typename... Args>
concept async_function =
    std::invocable<Fn, Args...> && future_type<std::invoke_result_t<std::remove_cvref_t<Fn>, Args...>>;

}  // namespace concepts

#define runtime_initializer runtime_initializer

};  // namespace asco
