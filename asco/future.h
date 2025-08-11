// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <coroutine>
#include <cstring>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <semaphore>
#include <type_traits>
#include <utility>

#include <asco/core/runtime.h>
#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/coroutine_allocator.h>
#include <asco/perf.h>
#include <asco/rterror.h>
#include <asco/utils/channel.h>
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

template<move_secure T, bool Inline, bool Blocking>
struct future_base {
    static_assert(!Inline || !Blocking, "[ASCO] Inline coroutine cannot be blocking.");

    using worker = RT::__worker;

    struct promise_type;
    using corohandle = std::coroutine_handle<promise_type>;
    using return_type = T;

    template<move_secure U = T>
    struct return_value_storage {
        char value[sizeof(U)];
    };

    template<>
    struct return_value_storage<void> {};

    struct future_state {
        std::exception_ptr e;

        atomic_size_t caller_task_id{0};
        std::coroutine_handle<> caller_task;

        atomic_bool returned{false};
        atomic_bool moved_back{false};
        return_value_storage<> return_value;
    };

    struct promise_base {
        size_t future_type_hash{inner::type_hash<future_base<T, Inline, Blocking>>()};

        size_t task_id{};

        std::shared_ptr<future_state> state{std::make_shared<future_state>()};

        void *operator new(std::size_t n) noexcept { return coroutine_allocator::allocate(n); }

        void operator delete(void *p) noexcept { coroutine_allocator::deallocate(p); }

        virtual corohandle corohandle_from_promise() = 0;

        template<size_t Hash>
        static bool group_local_exists() {
            if (!worker::in_worker())
                return false;
            auto &rt = RT::get_runtime();
            auto curid = worker::get_worker().current_task_id();
            if (!rt.in_group(curid))
                return false;
            return rt.group(curid)->var_exists<Hash>();
        }

        corohandle spawn(__coro_local_frame *curr_clframe, unwind::coro_trace trace) {
            auto &rt = RT::get_runtime();
            auto coro = corohandle_from_promise();

            if constexpr (!Inline) {
                if constexpr (Blocking)
                    task_id = rt.spawn_blocking(coro, curr_clframe, trace);
                else
                    task_id = rt.spawn(coro, curr_clframe, trace);
            } else if (!worker::in_worker()) {
                if constexpr (Blocking)
                    task_id = rt.spawn_blocking(coro, curr_clframe, trace);
                else
                    task_id = rt.spawn(coro, curr_clframe, trace);
            } else {
                using state = worker::scheduler::task_control::state;
                auto &worker = worker::get_worker();

                if constexpr (Blocking) {
                    auto t = rt.to_task(coro, Blocking, curr_clframe, trace);
                    t.is_inline = true;
                    task_id = t.id;
                    worker.sc.push_task(t, state::ready);
                } else {
                    auto t = rt.to_task(coro, Blocking, curr_clframe, trace);
                    t.is_inline = true;
                    task_id = t.id;
                    worker.sc.push_task(std::move(t), state::ready);
                }

                if (worker.is_calculator)
                    rt.inc_calcu_load();
                else
                    rt.inc_io_load();

                worker::insert_task_map(task_id, &worker);
            }

            if (worker::in_worker()) {
                auto &worker = worker::get_worker();
                auto currid = worker.current_task_id();
                if (rt.in_group(currid) && rt.group(currid)->is_origin(currid)
                    && group_local_exists<inner::__consteval_str_hash("__asco_select_sem__")>()) {
                    rt.join_task_to_group(task_id, currid);
                }
            }

            return coro;
        }

        future_base<T, Inline, Blocking> get_return_object() {
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
            return future_base<T, Inline, Blocking>(coro, task_id, ani, state);
        }

        std::suspend_always initial_suspend() { return {}; }

        bool return_value_select_case_impl() {
            auto &rt = RT::get_runtime();
            task_id = worker::get_worker().current_task_id();
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (group_local_exists<inner::__consteval_str_hash("__asco_select_sem__")>()
                && !rt.group(task_id)->is_origin(task_id)) {
                std::binary_semaphore group_local(__asco_select_sem__);
                if (__asco_select_sem__.try_acquire()) {
                    for (auto id : rt.group(task_id)->non_origin_tasks()) {
                        if (id != task_id) {
                            RT::get_runtime().abort(id);
                            rt.awake(id);
                        }
                    }
                } else {
                    return true;
                }
            }
            return false;
        }

        // The inline future will return the caller_resumer to symmatrical transform to the caller task.
        auto final_suspend() noexcept {
            auto caller_task_id = state->caller_task_id.load();
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (group_local_exists<inner::__consteval_str_hash("__asco_select_sem__")>()) {
                auto &rt = RT::get_runtime();
                rt.exit_group(task_id);
                if (caller_task_id)
                    rt.exit_group(caller_task_id);

                if (!worker::task_available(task_id))
                    return std::suspend_always{};

                if (auto &worker = worker::get_worker_from_task_id(task_id);
                    worker.sc.get_task(task_id).aborted) {
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
                while (!task_id) {}
                if (caller_task_id) {
                    rt.awake(caller_task_id);
                    RT::__worker::get_worker_from_task_id(caller_task_id)
                        .sc.get_task(caller_task_id)
                        .waiting.store(0);
                }

                if (worker::task_available(task_id))
                    rt.suspend(task_id);

                return std::suspend_always{};
            } else {
                auto &rt = RT::get_runtime();

                if (worker::task_available(task_id))
                    rt.suspend(task_id);

                rt.remove_task_map(corohandle_from_promise().address());
                asco::core::worker::remove_task_map(task_id);
                if (caller_task_id) {
                    rt.awake(caller_task_id);
                    worker::get_worker_from_task_id(caller_task_id)
                        .sc.get_task(caller_task_id)
                        .waiting.store(0);
                }

                return std::suspend_always{};
            }
        }

        void unhandled_exception() {
            state->e = std::current_exception();
            state->returned.store(true);
        }

        T retval_move_out() {
            if (!state->returned)
                throw asco::runtime_error(
                    "[ASCO] Can't move back return value bacause coroutine didn't return.");

            state->moved_back.store(true);
            if constexpr (!std::is_void_v<T>)
                return std::move(*reinterpret_cast<T *>(&state->return_value));
        }

        void set_abort_exception() { state->e = std::make_exception_ptr(coroutine_abort{}); }
    };

    struct return_value_mixin_promise : promise_base {
        void return_value(std::conditional_t<std::is_void_v<T>, std::monostate, T> val)
            requires(!std::is_void_v<T>)
        {
            if (promise_base::return_value_select_case_impl())
                return;

            new (static_cast<void *>(&promise_base::state->return_value)) T(std::move(val));
            promise_base::state->returned.store(true);
        }
    };

    struct return_void_mixin_promise : promise_base {
        void return_void()
            requires(std::is_void_v<T>)
        {
            if (promise_base::return_value_select_case_impl())
                return;

            promise_base::state->returned.store(true);
        }
    };

    struct promise_type
            : std::conditional_t<std::is_void_v<T>, return_void_mixin_promise, return_value_mixin_promise> {
        corohandle corohandle_from_promise() override { return corohandle::from_promise(*this); }
    };

    bool await_ready() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");
        return state->returned.load();
    }

    // The inline future will return current task corohandle to symmatrical transform to the callee task.
    auto await_suspend(std::coroutine_handle<> handle) {
        if constexpr (!Inline) {
            if (!state->returned.load()) {
                auto &rt = RT::get_runtime();
                auto id = rt.task_id_from_corohandle(handle);
                state->caller_task = handle;
                state->caller_task_id.store(id);
                worker::get_worker_from_task_id(id).sc.get_task(id).waiting.store(task_id);
                rt.suspend(id);
                return true;
            }
            return false;
        } else {
            auto &rt = RT::get_runtime();
            auto &worker = worker::get_worker();
            auto id = rt.task_id_from_corohandle(handle);
            state->caller_task = handle;
            state->caller_task_id.store(id);
            worker::get_worker_from_task_id(id).sc.get_task(id).waiting.store(task_id);
            rt.suspend(id);

            auto &task_ = worker.sc.get_task(task_id);
#ifdef ASCO_PERF_RECORD
            task_.perf_recorder->record_once();
#endif
            worker.running_task.push(&task_);
            worker.sc.awake(task_id);
            return task;
        }
    }

    T await_resume() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");

        if (auto &e = state->e) {
            auto tmp = e;
            e = nullptr;
            std::rethrow_exception(tmp);
        }

        if constexpr (!std::is_void_v<T>)
            return std::move(*reinterpret_cast<T *>(&state->return_value));
    }

    T await() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");

        if (worker::in_worker())
            throw asco::runtime_error("[ASCO] Cannot use synchronized await in asco::runtime workers");

        if constexpr (!Inline) {
            if constexpr (!std::is_void_v<T>) {
                if (state->returned.load())
                    return std::move(*reinterpret_cast<T *>(&state->return_value));
            }

            RT::get_runtime().register_sync_awaiter(task_id);
            worker::get_worker_from_task_id(task_id).sc.get_sync_awaiter(task_id).acquire();

            if (auto &e = state->e) {
                auto tmp = e;
                e = nullptr;
                std::rethrow_exception(tmp);
            }

            if constexpr (!std::is_void_v<T>)
                return std::move(*reinterpret_cast<T *>(&state->return_value));
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
        none = false;
        rhs.none = true;
    }

    future_base(corohandle task, size_t task_id, bool ani, std::shared_ptr<future_state> state)
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
        }

        if (auto &e = state->e) {
            try {
                std::rethrow_exception(e);
            } catch (coroutine_abort &) {
            } catch (...) { std::rethrow_exception(std::current_exception()); }
        }
    }

    future_base &operator=(future_base &&rhs) {
        if (!none) {
            if (auto &e = state->e)
                std::rethrow_exception(e);
            none = true;
        }

        if (!rhs.none) {
            std::swap(task, rhs.task);
            std::swap(task_id, rhs.task_id);
            std::swap(state, rhs.state);
            none = false;
            rhs.none = true;
        }
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

        if constexpr (!std::is_void_v<T>) {
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

    template<async_function<return_type> F>
    future_base<typename std::invoke_result_t<F, return_type>::return_type, false, Blocking>
    then(this future_base self, F f) {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        if (worker::get_worker().current_task().aborted)
            throw coroutine_abort{};

        auto last_result = co_await self;

        if (worker::get_worker().current_task().aborted)
            throw coroutine_abort{};

        auto current_result = co_await f(std::move(last_result));

        if (worker::get_worker().current_task().aborted)
            throw coroutine_abort{};

        co_return std::move(current_result);
    }

    template<exception_handler F>
    using exceptionally_expected_error_t = std::conditional_t<
        std::is_void_v<std::invoke_result_t<F, exception_type<F>>>, std::monostate,
        std::invoke_result_t<F, exception_type<F>>>;

    template<exception_handler F>
    future_base<std::expected<return_type, exceptionally_expected_error_t<F>>, false, Blocking>
    exceptionally(this future_base self, F f) {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        try {
            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            auto result = co_await self;

            if (worker::get_worker().current_task().aborted)
                throw coroutine_abort{};

            co_return std::move(result);
        } catch (first_argument_t<F> e) {
            if constexpr (!std::is_void_v<std::invoke_result_t<F, exception_type<F>>>) {
                co_return std::unexpected{f(e)};
            } else {
                f(e);
                co_return std::unexpected{std::monostate{}};
            }
        } catch (...) { std::rethrow_exception(std::current_exception()); }
    }

    template<typename U>
    using aborted_optional_value_t = std::conditional_t<std::is_void_v<U>, std::monostate, U>;

    template<std::invocable F>
    future_base<std::optional<aborted_optional_value_t<return_type>>, false, Blocking>
    aborted(this future_base self, F f) {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        try {
            if constexpr (!std::is_void_v<T>)
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

    // If a inline future function called at where it is not worker thread, it actually will spawn to a worker
    // thread.
    bool actually_non_inline;

    std::shared_ptr<future_state> state;

    bool none{true};
};  // namespace asco::base

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

#endif
};  // namespace asco::base

namespace asco {

using base::future, base::future_inline, base::future_core;

using base::runtime_initializer_t;

using base::coroutine_abort;

};  // namespace asco

#include <asco/futures.h>

#endif
