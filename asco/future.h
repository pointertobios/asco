// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <coroutine>
#include <cstring>
#include <exception>
#include <expected>
#include <functional>
#include <optional>
#include <semaphore>
#include <type_traits>
#include <utility>

#include <asco/compile_time/string.h>
#include <asco/core/runtime.h>
#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/coroutine_allocator.h>
#include <asco/io/buffer.h>
#include <asco/perf.h>
#include <asco/rterror.h>
#include <asco/utils/concepts.h>
#include <asco/utils/pubusing.h>
#include <variant>

#if defined(_MSC_VER) && !defined(__clang__)
#    error "[ASCO] Compile with clang-cl instead of MSVC"
#endif

namespace asco::base {

using namespace types;
using namespace concepts;

struct coroutine_abort : std::exception {};

template<move_secure T, bool Inline, bool Blocking, size_t ExtraSpace = 0>
struct future_base {
    static_assert(!Inline || !Blocking, "[ASCO] Inline coroutine cannot be blocking.");

    using worker = RT::__worker;

    struct promise_type;
    using corohandle = std::coroutine_handle<>;
    using return_type = T;

    template<move_secure U = return_type>
    struct return_value_storage {
        char value[ExtraSpace ? 0 : sizeof(U)];
    };

    template<>
    struct return_value_storage<void> {};

    using extra_space_destructor = void (*)(void *);

    struct future_state {
    public:
        std::exception_ptr e;

        atomic_size_t caller_task_id{0};
        std::coroutine_handle<> caller_task;

        atomic_bool e_rethrowed{false};
        atomic_bool returned{false};
        atomic_bool moved_back{false};

    private:
        atomic_bool refcount{true};  // Count can only be handled by promise and future. true: 2, false: 1

    public:
        return_value_storage<> return_value;
        bool has_return_value{true};

        char extra_space[ExtraSpace];

        extra_space_destructor extra_space_dtor{nullptr};

        future_state(const future_state &) = delete;
        future_state &operator=(const future_state &) = delete;

        future_state(future_state &&) = default;
        future_state &operator=(future_state &&) = default;

        static future_state *create() noexcept { return new future_state; }

        void unlink() noexcept {
            if (auto rc = true; !refcount.compare_exchange_strong(rc, false))
                delete this;
        }

    private:
        future_state() = default;

        ~future_state() noexcept {
            if (extra_space_dtor)
                extra_space_dtor(extra_space);
        }

        void *operator new(std::size_t) noexcept {
            if constexpr (!std::is_same_v<decltype(future_base::future_state_slub_cache), std::monostate>)
                return future_base::future_state_slub_cache.allocate();
            else
                return ::operator new(sizeof(future_state));
        }

        void operator delete(void *ptr) noexcept {
            if constexpr (!std::is_same_v<decltype(future_base::future_state_slub_cache), std::monostate>)
                future_base::future_state_slub_cache.deallocate(static_cast<future_state *>(ptr));
            else
                ::operator delete(ptr);
        }
    };

    struct promise_base {
        size_t future_type_hash{inner::type_hash<future_base<return_type, Inline, Blocking>>()};

        atomic_size_t task_id{0};

        future_state *state{future_state::create()};

        void *operator new(std::size_t n) noexcept { return coroutine_allocator::allocate(n); }

        void operator delete(void *p) noexcept { coroutine_allocator::deallocate(p); }

        ~promise_base() noexcept { state->unlink(); }

        virtual corohandle corohandle_from_promise() = 0;

        template<compile_time::string Name>
        static bool group_local_exists() {
            if (!worker::in_worker())
                return false;
            auto &rt = RT::get_runtime();
            auto curid = worker::get_worker().current_task_id();
            if (!rt.in_group(curid))
                return false;
            return rt.group(curid)->var_exists<Name>();
        }

        corohandle spawn(__coro_local_frame *curr_clframe, unwind::coro_trace trace) {
            auto &rt = RT::get_runtime();
            auto coro = corohandle_from_promise();

            if constexpr (!Inline) {
                if constexpr (Blocking)
                    task_id.store(rt.spawn_blocking(coro, curr_clframe, trace));
                else
                    task_id.store(rt.spawn(coro, curr_clframe, trace));
            } else if (!worker::in_worker()) {
                if constexpr (Blocking)
                    task_id.store(rt.spawn_blocking(coro, curr_clframe, trace));
                else
                    task_id.store(rt.spawn(coro, curr_clframe, trace));
            } else {
                using state = core::sched::task::state;
                auto &worker = worker::get_worker();

                if constexpr (Blocking) {
                    auto t = rt.to_task(coro, Blocking, curr_clframe, trace);
                    t->is_inline = true;
                    task_id.store(t.id);
                    worker.sc.push_task(t, state::ready);
                } else {
                    auto t = rt.to_task(coro, Blocking, curr_clframe, trace);
                    t->is_inline = true;
                    task_id.store(t->id);
                    worker.sc.push_task(std::move(t), state::ready);
                }

                if (worker.is_calculator)
                    rt.inc_calcu_load();
                else
                    rt.inc_io_load();

                worker::insert_task_map(task_id.load(), &worker);
            }

            if (worker::in_worker()) {
                auto &worker = worker::get_worker();
                auto currid = worker.current_task_id();
                if (rt.in_group(currid) && rt.group(currid)->is_origin(currid)
                    && group_local_exists<"__asco_select_sem__">()) {
                    rt.join_task_to_group(task_id.load(), currid);
                }
            }

            return coro;
        }

        future_base get_return_object() {
            void *trace_addr = unwind::unwind_index(2);  // This addr is only correct when compile with -O0
            unwind::coro_trace *trace_prev_addr = nullptr;
            __coro_local_frame *curr_clframe = nullptr;

            bool ani = false;

            if (worker::in_worker()) {
                auto &currtask = worker::get_worker().current_task();
                curr_clframe = currtask.coro_local_frame;
                trace_prev_addr = &currtask.coro_local_frame->tracing_stack;
            } else {
                ani = true;
            }

            auto coro = spawn(curr_clframe, {trace_addr, trace_prev_addr});
            return future_base(coro, task_id.load(), ani, state);
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        // Always returns suspend_always{}, we have to call .destroy() in scheduler.
        auto final_suspend() noexcept {
            auto caller_task_id = state->caller_task_id.load();
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (group_local_exists<"__asco_select_sem__">()) {
                auto &rt = RT::get_runtime();
                rt.exit_group(task_id.load());
                if (caller_task_id)
                    rt.exit_group(caller_task_id);

                if (!worker::task_available(task_id.load()))
                    return std::suspend_always{};

                if (auto &worker = worker::get_worker_from_task_id(task_id.load());
                    worker.sc.get_task(task_id.load())->aborted) {
                    if (worker::task_available(caller_task_id)) {
                        auto &w = worker::get_worker_from_task_id(caller_task_id);
                        w.sc.free_only(caller_task_id, true);
                        if (w.is_calculator)
                            rt.dec_calcu_load();
                        else
                            rt.dec_io_load();
                        rt.remove_task_map(state->caller_task.address());
                        worker::remove_task_map(caller_task_id);
                    }
                    return std::suspend_always{};
                }
            }

            if constexpr (!Inline) {
                auto &rt = RT::get_runtime();
                while (!task_id.load());
                if (caller_task_id) {
                    RT::__worker::get_worker_from_task_id(caller_task_id)
                        .sc.get_task(caller_task_id)
                        ->waiting.store(0, morder::release);
                    rt.awake(caller_task_id);
                }

                if (auto tid = task_id.load(); worker::task_available(tid))
                    rt.suspend(tid);

                return std::suspend_always{};
            } else {
                auto &rt = RT::get_runtime();

                if (auto tid = task_id.load(); worker::task_available(tid))
                    rt.suspend(tid);

                if (caller_task_id) {
                    worker::get_worker_from_task_id(caller_task_id)
                        .sc.get_task(caller_task_id)
                        ->waiting.store(0, morder::release);
                    rt.awake(caller_task_id);
                }

                return std::suspend_always{};
            }
        }

        void unhandled_exception() {
            state->e = std::current_exception();
            state->returned.store(true);
        }

        return_type retval_move_out() {
            if (!state->returned)
                throw asco::runtime_error(
                    "[ASCO] Can't move back return value bacause coroutine didn't return.");

            state->moved_back.store(true);
            if constexpr (!std::is_void_v<return_type>)
                return std::move(*reinterpret_cast<return_type *>(&state->return_value));
        }

        void set_abort_exception() { state->e = std::make_exception_ptr(coroutine_abort{}); }

        bool handle_select_winner() {
            auto &rt = RT::get_runtime();
            task_id.store(worker::get_worker().current_task_id());
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (auto tid = task_id.load();
                group_local_exists<"__asco_select_sem__">() && !rt.group(tid)->is_origin(tid)) {
                std::binary_semaphore group_local(__asco_select_sem__);
                if (__asco_select_sem__.try_acquire()) {
                    for (auto id : rt.group(tid)->non_origin_tasks()) {
                        if (id != tid)
                            RT::get_runtime().abort(id);
                    }
                } else {
                    return true;
                }
            }
            return false;
        }
    };

    struct return_value_mixin_promise : promise_base {
        void return_value(std::conditional_t<std::is_void_v<return_type>, std::monostate, return_type> &&val)
            requires(!std::is_void_v<return_type>)
        {
            if (promise_base::handle_select_winner())
                return;

            new (static_cast<void *>(&promise_base::state->return_value)) return_type(std::move(val));
            promise_base::state->returned.store(true);
        }
    };

    struct return_void_mixin_promise : promise_base {
        void return_void()
            requires(std::is_void_v<return_type>)
        {
            if (promise_base::handle_select_winner())
                return;

            promise_base::state->returned.store(true);
        }
    };

    struct promise_type
            : std::conditional_t<
                  std::is_void_v<return_type>, return_void_mixin_promise, return_value_mixin_promise> {
        corohandle corohandle_from_promise() override {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }
    };

    bool await_ready() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");
        return state->returned.load();
    }

    auto await_suspend(std::coroutine_handle<> handle) {
        if constexpr (!Inline) {
            if (!state->returned.load()) {
                auto &rt = RT::get_runtime();
                auto id = rt.task_id_from_corohandle(handle);
                state->caller_task = handle;
                state->caller_task_id.store(id);
                worker::get_worker_from_task_id(id).sc.get_task(id)->waiting.store(task_id, morder::release);
                rt.suspend(id);
                return true;
            }
            return false;
        } else {
            auto &worker = worker::get_worker();
            if (auto &w = worker::get_worker_from_task_id(task_id); w.id != worker.id) {
                if (auto res = w.sc.steal(task_id)) {
                    auto [task, awaiter] = *res;
                    worker.modify_task_map(task_id, &worker);
                    worker.sc.steal_from(task, awaiter);
                }
            }

            auto &rt = RT::get_runtime();
            auto id = rt.task_id_from_corohandle(handle);
            state->caller_task = handle;
            state->caller_task_id.store(id);
            worker::get_worker_from_task_id(id).sc.get_task(id)->waiting.store(task_id, morder::release);
            rt.suspend(id);

            auto task_ = worker.sc.get_task(task_id);
#ifdef ASCO_PERF_RECORD
            task_->perf_recorder->record_once();
#endif
            worker.running_task.push(task_);
            worker.sc.awake(task_id);
            return task;
        }
    }

    return_type await_resume() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");

        if (auto &e = state->e) {
            auto tmp = std::move(e);
            state->e_rethrowed.store(true, morder::release);
            std::rethrow_exception(tmp);
        }

        if constexpr (!std::is_void_v<return_type>)
            return std::move(*reinterpret_cast<return_type *>(&state->return_value));
        else
            return;
    }

    return_type await() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");

        if constexpr (!Inline) {
            if constexpr (!std::is_void_v<return_type>) {
                if (state->returned.load())
                    return std::move(*reinterpret_cast<return_type *>(&state->return_value));
            }

            RT::get_runtime().register_sync_awaiter(task_id);
            worker::get_worker_from_task_id(task_id).sc.get_sync_awaiter(task_id).acquire();

            if (auto &e = state->e) {
                auto tmp = e;
                state->e_rethrowed.store(true, morder::release);
                std::rethrow_exception(tmp);
            }

            if constexpr (!std::is_void_v<return_type>)
                return std::move(*reinterpret_cast<return_type *>(&state->return_value));
            else
                return;
        } else {
            throw asco::runtime_error("[ASCO] future_inline<T> cannot be awaited in synchronized context.");
        }
        std::unreachable();
    }

    void abort() {
        if (none)
            return;

        RT::get_runtime().abort(task_id);
    }

    future_base() = default;

    future_base(const future_base &) = delete;

    future_base(future_base &&rhs) {
        if (rhs.none)
            return;

        std::swap(task, rhs.task);
        std::swap(task_id, rhs.task_id);
        std::swap(state, rhs.state);
        std::swap(actually_non_inline, rhs.actually_non_inline);
        none = false;
        rhs.none = true;
    }

    future_base(corohandle task, size_t task_id, bool ani, future_state *state)
            : task(task)
            , task_id(task_id)
            , actually_non_inline(ani)
            , state(state)
            , none(false) {
        if (!Inline)
            RT::get_runtime().awake(task_id);
    }

    ~future_base() {
        if (none)
            return;

        if (!state->returned.load()) {
            state->caller_task = nullptr;
            state->caller_task_id.store(0);
        } else if constexpr (!std::is_void_v<return_type>) {
            if (state->has_return_value)
                reinterpret_cast<return_type *>(&state->return_value)->~return_type();
        }

        auto e = state->e;
        bool e_rethrowed = state->e_rethrowed.load(morder::acquire);
        state->unlink();

        if (!e_rethrowed && e) {
            try {
                std::rethrow_exception(e);
            } catch (coroutine_abort &) {
            } catch (...) { std::rethrow_exception(std::current_exception()); }
        }
    }

    future_base &operator=(future_base &&rhs) {
        if (this == &rhs)
            return *this;

        this->~future_base();
        new (this) future_base(std::move(rhs));
        return *this;
    }

    future_base<return_type, false, Blocking> dispatch(this future_base self) {
        if (!self.actually_non_inline)
            throw asco::runtime_error("[ASCO] dispatch(): from an actually inline future.");

        auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
        auto &w = worker::get_worker();
        w.modify_task_map(self.task_id, &w);
        w.sc.steal_from(task, awaiter);

        if (worker::get_worker().current_task().aborted)
            throw coroutine_abort{};

        if constexpr (!std::is_void_v<return_type>) {
            auto res = co_await self;

            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            co_return std::move(res);
        } else {
            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            co_return;
        }
    }

    auto then(this future_base self, async_function<return_type> auto f) -> future_base<
        typename std::invoke_result_t<decltype(f), return_type>::return_type, false, Blocking> {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        if (worker::get_worker().current_task().aborted)
            throw coroutine_abort{};

        if constexpr (!std::is_void_v<return_type>) {
            auto last_result = co_await self;

            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            auto current_result = co_await f(std::move(last_result));

            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            co_return std::move(current_result);
        } else {
            co_await self;

            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            auto current_result = co_await f();

            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            co_return std::move(current_result);
        }
    }

    template<exception_handler F>
    using exceptionally_expected_error_t = std::conditional_t<
        std::is_void_v<std::invoke_result_t<F, exception_type<F>>>, std::monostate,
        std::invoke_result_t<F, exception_type<F>>>;

    auto exceptionally(this future_base self, exception_handler auto f) -> future_base<
        std::expected<monostate_if_void<return_type>, exceptionally_expected_error_t<decltype(f)>>, false,
        Blocking> {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        try {
            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};
            if constexpr (!std::is_void_v<return_type>) {
                auto result = co_await self;

                if (worker::get_worker().current_task().aborted)
                    throw coroutine_abort{};

                co_return std::move(result);
            } else {
                co_await self;

                if (worker::get_worker().current_task().aborted)
                    throw coroutine_abort{};

                co_return;
            }
        } catch (exception_type<decltype(f)> e) {
            if constexpr (!std::is_void_v<std::invoke_result_t<decltype(f), exception_type<decltype(f)>>>) {
                co_return std::unexpected{f(e)};
            } else {
                f(e);
                co_return std::unexpected{std::monostate{}};
            }
        } catch (...) { std::rethrow_exception(std::current_exception()); }
    }

    future_base<std::optional<monostate_if_void<return_type>>, false, Blocking>
    aborted(this future_base self, std::invocable auto f) {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        try {
            if constexpr (!std::is_void_v<return_type>)
                co_return co_await self;
            else {
                co_await self;
                co_return std::monostate{};
            }
        } catch (coroutine_abort &) {
            f();
            co_return std::nullopt;
        } catch (...) { std::rethrow_exception(std::current_exception()); }
    }

private:
    corohandle task;
    size_t task_id;

    // If a inline future function called at where it is not worker thread, it shall be spawned to a worker
    // thread through calling dispatch().
    bool actually_non_inline;

protected:
    future_state *state{nullptr};

    bool none{true};

private:
    static std::conditional_t<
        sizeof(future_state) <= core::slub::page<future_state>::largest_obj_size,
        core::slub::cache<future_state>, std::monostate>
        future_state_slub_cache;
};

template<move_secure T, bool Inline, bool Blocking, size_t ExtraSpace>
inline std::conditional_t<
    sizeof(typename future_base<T, Inline, Blocking, ExtraSpace>::future_state) <= core::slub::page<
        typename future_base<T, Inline, Blocking, ExtraSpace>::future_state>::largest_obj_size,
    core::slub::cache<typename future_base<T, Inline, Blocking, ExtraSpace>::future_state>, std::monostate>
    future_base<T, Inline, Blocking, ExtraSpace>::future_state_slub_cache{};

template<move_secure T>
using future = future_base<T, false, false>;

template<move_secure T>
using future_inline = future_base<T, true, false>;

template<move_secure T>
using future_core = future_base<T, false, true>;

using runtime_initializer_t = std::optional<std::function<RT *()>>;

#ifndef FUTURE_IMPL

extern template struct future_base<void, false, false>;
extern template struct future_base<void, true, false>;
extern template struct future_base<void, false, true>;

extern template struct future_base<int, false, false>;
extern template struct future_base<int, true, false>;
extern template struct future_base<int, false, true>;

extern template struct future_base<std::string, false, false>;
extern template struct future_base<std::string, true, false>;
extern template struct future_base<std::string, false, true>;

extern template struct future_base<std::string_view, false, false>;
extern template struct future_base<std::string_view, true, false>;
extern template struct future_base<std::string_view, false, true>;

extern template struct future_base<io::buffer<>, false, false>;
extern template struct future_base<io::buffer<>, true, false>;
extern template struct future_base<io::buffer<>, false, true>;

extern template struct future_base<std::optional<io::buffer<>>, false, false>;
extern template struct future_base<std::optional<io::buffer<>>, true, false>;
extern template struct future_base<std::optional<io::buffer<>>, false, true>;

#endif

};  // namespace asco::base

namespace asco {

using base::future, base::future_inline, base::future_core;

using base::runtime_initializer_t;

using base::coroutine_abort;

};  // namespace asco

#include <asco/futures.h>

#endif
