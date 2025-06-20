// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <coroutine>
#include <cstring>
#include <expected>
#include <functional>
#include <iostream>
#include <optional>
#include <semaphore>
#include <utility>

#include <asco/core/runtime.h>
#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/perf.h>
#include <asco/rterror.h>
#include <asco/utils/channel.h>
#include <asco/utils/concepts.h>
#include <asco/utils/pubusing.h>

#if defined(_MSC_VER) && !defined(__clang__)
#    error "[ASCO] Compile with clang-cl instead of MSVC"
#endif

namespace asco::base {

struct coroutine_abort : std::exception {};

using core::runtime_type;

struct _future_void {};

template<move_secure T, bool Inline, bool Blocking, runtime_type R = RT>
struct future_base {
    static_assert(!std::is_void_v<T>, "[ASCO] Use asco::future_void instead.");
    static_assert(!Inline || !Blocking, "[ASCO] Inline coroutine cannot be blocking.");

    using worker = RT::__worker;

    struct promise_type;
    using corohandle = std::coroutine_handle<promise_type>;
    using return_type = T;

    struct promise_type {
        size_t future_type_hash{type_hash<future_base<T, Inline, Blocking>>()};

        size_t task_id{};
        std::binary_semaphore awaiter_sem{0};

        future_base *awaiter{nullptr};
        std::coroutine_handle<> caller_task;
        size_t caller_task_id{0};

        inner::sender<T, 1> return_sender;

        void *operator new(std::size_t n) noexcept {
            auto *p = static_cast<size_t *>(::operator new(n + 2 * sizeof(size_t)));
            *p = n;
            return p + 2;
        }

        void operator delete(void *p) noexcept {
            size_t *q = static_cast<size_t *>(p) - 2;
            ::operator delete(q);
        }

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
            auto coro = corohandle::from_promise(*this);

            if constexpr (!Inline) {
                if constexpr (Blocking)
                    task_id = rt.spawn_blocking(coro, curr_clframe, trace);
                else
                    task_id = rt.spawn(coro, curr_clframe, trace);
            } else {
                using state = worker::scheduler::task_control::__control_state;
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
                    worker.sc.push_task(t, state::ready);
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
                    && group_local_exists<__consteval_str_hash("__asco_select_sem__")>()) {
                    rt.join_task_to_group(task_id, currid);
                }
            }

            return coro;
        }

        future_base<T, Inline, Blocking> get_return_object() {
            void *trace_addr = unwind::unwind_index(2);  // This addr is only correct when compile with -O0
            unwind::coro_trace *trace_prev_addr = nullptr;
            __coro_local_frame *curr_clframe = nullptr;
            if (worker::in_worker()) {
                auto &currtask = worker::get_worker().current_task();
                curr_clframe = currtask.coro_local_frame;
                trace_prev_addr = &currtask.coro_local_frame->tracing_stack;
            }
            auto coro = spawn(curr_clframe, {trace_addr, trace_prev_addr});
            auto [tx, rx] = inner::ss::channel<T, 1>();
            return_sender = std::move(tx);
            return future_base<T, Inline, Blocking>(coro, task_id, std::move(rx));
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            auto &rt = RT::get_runtime();
            task_id = worker::get_worker().current_task_id();
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (group_local_exists<__consteval_str_hash("__asco_select_sem__")>()
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
                    return;
                }
            }

            return_sender.send(std::move(val));

            if (awaiter)
                awaiter->promise_returned = true;
        }

        // The inline future will return the caller_resumer to symmatrical transform to the caller task.
        auto final_suspend() noexcept {
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (group_local_exists<__consteval_str_hash("__asco_select_sem__")>()) {
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
                        w.sc.destroy(caller_task_id, true);
                        if (w.is_calculator)
                            rt.dec_calcu_load();
                        else
                            rt.dec_io_load();
                        rt.remove_task_map(caller_task.address());
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
                        .waiting = 0;
                }

                if (worker::task_available(task_id))
                    rt.suspend(task_id);

                return std::suspend_always{};
            } else {
                auto &rt = RT::get_runtime();

                if (worker::task_available(task_id))
                    rt.suspend(task_id);

                rt.remove_task_map(corohandle::from_promise(*this).address());
                asco::core::worker::remove_task_map(task_id);
                rt.awake(caller_task_id);
                worker::get_worker_from_task_id(caller_task_id).sc.get_task(caller_task_id).waiting = 0;

                return std::suspend_always{};
            }
        }

        void unhandled_exception() {
            if (!awaiter)
                std::rethrow_exception(std::current_exception());
            awaiter_sem.acquire();
            awaiter->e = std::current_exception();
            awaiter->promise_returned = true;
            awaiter_sem.release();
        }
    };

    bool await_ready() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");
        return promise_returned;
    }

    // The inline future will return current task corohandle to symmatrical transform to the callee task.
    auto await_suspend(std::coroutine_handle<> handle) {
        if constexpr (!Inline) {
            if (!promise_returned) {
                auto &rt = RT::get_runtime();
                auto id = rt.task_id_from_corohandle(handle);
                task.promise().caller_task = handle;
                task.promise().caller_task_id = id;
                worker::get_worker_from_task_id(id).sc.get_task(id).waiting = task_id;
                rt.suspend(id);
                return true;
            }
            return false;
        } else {
            auto &rt = RT::get_runtime();
            auto &worker = worker::get_worker();
            auto id = rt.task_id_from_corohandle(handle);
            task.promise().caller_task = handle;
            task.promise().caller_task_id = id;
            worker::get_worker_from_task_id(id).sc.get_task(id).waiting = task_id;
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

        if (e) {
            auto tmp = e;
            e = nullptr;
            std::rethrow_exception(tmp);
        }
        return std::move(*return_receiver.recv());
    }

    T await() {
        if (none)
            throw asco::runtime_error("[ASCO] future didn't bind to a task");

        if (worker::in_worker())
            throw asco::runtime_error("[ASCO] Cannot use synchronized await in asco::runtime workers");

        if constexpr (!Inline) {
            if (promise_returned)
                return std::move(*return_receiver.recv());

            RT::get_runtime().register_sync_awaiter(task_id);
            worker::get_worker_from_task_id(task_id).sc.get_sync_awaiter(task_id).acquire();

            if (e) {
                auto tmp = e;
                e = nullptr;
                std::rethrow_exception(tmp);
            }
            return std::move(*return_receiver.recv());
        } else {
            static_assert(false, "[ASCO] future_inline<T> cannot be awaited in synchronized context.");
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
        task.promise().awaiter = this;
        std::swap(task_id, rhs.task_id);
        return_receiver = std::move(rhs.return_receiver);
        std::swap(e, rhs.e);
        std::swap(promise_returned, rhs.promise_returned);
        none = false;
        rhs.none = true;
    }

    future_base(corohandle task, size_t task_id, inner::receiver<T, 1> &&rx)
            : task(task)
            , task_id(task_id)
            , return_receiver(std::move(rx))
            , none(false) {
        task.promise().awaiter = this;
        task.promise().awaiter_sem.release();
        if (!Inline)
            RT::get_runtime().awake(task_id);
    }

    ~future_base() {
        if (none)
            return;

        if (!promise_returned)
            task.promise().awaiter = nullptr;

        if (e) {
            try {
                std::rethrow_exception(e);
            } catch (coroutine_abort &) {
            } catch (...) { std::rethrow_exception(std::current_exception()); }
        }
    }

    future_base &operator=(future_base &&rhs) {
        if (!none) {
            if (!promise_returned)
                task.promise().awaiter = nullptr;
            if (e)
                std::rethrow_exception(e);
            none = true;
        }

        if (!rhs.none) {
            std::swap(task, rhs.task);
            task.promise().awaiter = this;
            std::swap(task_id, rhs.task_id);
            return_receiver = std::move(rhs.return_receiver);
            std::swap(e, rhs.e);
            std::swap(promise_returned, rhs.promise_returned);
            none = false;
            rhs.none = true;
        }
        return *this;
    }

    T retval_move_out() {
        if (none)
            throw asco::runtime_error("[ASCO] retval_move_out(): from a none future object.");

        none = true;
        return std::move(*return_receiver.recv());
    }

    void set_abort_exception() {
        if (none)
            throw asco::runtime_error("[ASCO] set_abort_exception(): from a none future object.");

        e = std::make_exception_ptr(coroutine_abort{});
    }

    template<async_function<return_type> F>
    future_base<typename std::invoke_result_t<F, return_type>::return_type, false, Blocking, R>
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
    future_base<std::expected<return_type, exceptionally_expected_error_t<F>>, false, Blocking, R>
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

    template<std::invocable F>
    future_base<std::optional<return_type>, false, Blocking, R> aborted(this future_base self, F f) {
        if (Inline) {
            auto [task, awaiter] = *worker::get_worker_from_task_id(self.task_id).sc.steal(self.task_id);
            auto &w = worker::get_worker();
            w.modify_task_map(self.task_id, &w);
            w.sc.steal_from(task, awaiter);
        }

        try {
            co_return std::move(co_await self);
        } catch (coroutine_abort &) {
            f();
            co_return std::nullopt;
        } catch (...) { std::rethrow_exception(std::current_exception()); }
    }

private:
    corohandle task;
    size_t task_id;

    std::exception_ptr e;
    bool promise_returned{false};

    inner::receiver<T, 1> return_receiver;

    bool none{true};
};

template<move_secure T, runtime_type R = RT>
using future = future_base<T, false, false, R>;

template<move_secure T, runtime_type R = RT>
using future_inline = future_base<T, true, false, R>;

template<move_secure T, runtime_type R = RT>
using future_core = future_base<T, false, true, R>;

using future_void = future<_future_void>;
using future_void_inline = future_inline<_future_void>;
using future_void_core = future_core<_future_void>;

using runtime_initializer_t = std::optional<std::function<RT *()>>;

};  // namespace asco::base

namespace asco {

using base::future, base::future_inline, base::future_core;
using base::future_void, base::future_void_inline, base::future_void_core;

using base::runtime_initializer_t;

using base::coroutine_abort;

};  // namespace asco

#include <asco/futures.h>

#endif
