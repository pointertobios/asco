// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <coroutine>
#include <functional>
#include <optional>
#include <semaphore>

#include <asco/core/runtime.h>
#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/utils/concepts.h>
#include <asco/utils/pubusing.h>

#if defined(_MSC_VER) && !defined(__clang__)
#    error "[ASCO] Compile with clang-cl instead of MSVC"
#endif

namespace asco::base {

using core::is_runtime;

struct _future_void {};

template<typename R = RT>
    requires is_runtime<R>
R *get_runtime() {
    return R::get_runtime();
}

template<typename T, bool Inline, bool Blocking, typename R = RT>
    requires is_move_secure_v<T> && is_runtime<R>
struct future_base {
    static_assert(!std::is_void_v<T>, "[ASCO] Use asco::future_void instead.");
    static_assert(!Inline || !Blocking, "[ASCO] Inline coroutine cannot be blocking.");

    using worker = RT::__worker;

    struct promise_type;
    using corohandle = std::coroutine_handle<promise_type>;
    using return_type = T;

    struct promise_type {
        // Always put this at the first, so that we can authenticate the coroutine_handle type when we
        // reinterpret it from raw pointer.
        size_t future_type_hash{type_hash<future_base<T, Inline, Blocking>>()};

        size_t task_id{};
        future_base *awaiter{nullptr};
        std::binary_semaphore awaiter_sem{0};

        std::coroutine_handle<> caller_task;
        size_t caller_task_id{0};

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

        corohandle spawn(__coro_local_frame *curr_clframe) {
            auto &rt = RT::get_runtime();
            auto coro = corohandle::from_promise(*this);

            if constexpr (!Inline) {
                if constexpr (Blocking)
                    task_id = rt.spawn_blocking(coro, curr_clframe);
                else
                    task_id = rt.spawn(coro, curr_clframe);
            } else {
                using state = worker::scheduler::task_control::__control_state;
                auto &worker = worker::get_worker();

                if constexpr (Blocking) {
                    auto t = rt.to_task(coro, Blocking, curr_clframe);
                    t.is_inline = true;
                    task_id = t.id;
                    worker.sc.push_task(t, state::suspending);
                } else {
                    auto t = rt.to_task(coro, Blocking, curr_clframe);
                    t.is_inline = true;
                    task_id = t.id;
                    worker.sc.push_task(t, state::suspending);
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
            __coro_local_frame *curr_clframe = nullptr;
            if (worker::in_worker()) {
                curr_clframe = worker::get_worker().current_task().coro_local_frame;
            }
            auto coro = spawn(curr_clframe);
            return future_base<T, Inline, Blocking>(coro, task_id);
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            auto &rt = RT::get_runtime();
            // Do NOT ONLY assert if this task is in a group, the origin coroutine do not need
            // `__asco_select_sem__` for continue running
            if (group_local_exists<__consteval_str_hash("__asco_select_sem__")>()
                && !rt.group(task_id)->is_origin(task_id)) {
                task_id = worker::get_worker().current_task_id();
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

            awaiter_sem.acquire();
            if (awaiter) {
                awaiter->retval = std::move(val);
                awaiter->promise_returned = true;
            }
            awaiter_sem.release();
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

    bool await_ready() { return promise_returned; }

    // The inline future will return current task corohandle to symmatrical transform
    // to the callee task.
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

            worker.running_task.push(&worker.sc.get_task(task_id));
            return task;
        }
    }

    T await_resume() {
        if (e)
            std::rethrow_exception(e);
        return std::move(retval);
    }

    T await() {
        if (worker::in_worker())
            throw std::runtime_error("[ASCO] Cannot use synchronized await in asco::runtime");

        if constexpr (!Inline) {
            if (promise_returned)
                return std::move(retval);

            RT::get_runtime().register_sync_awaiter(task_id);
            worker::get_worker_from_task_id(task_id).sc.get_sync_awaiter(task_id).acquire();

            if (e)
                std::rethrow_exception(e);
            return std::move(retval);
        } else {
            static_assert(false, "[ASCO] future_inline<T> cannot be awaited in synchronized context.");
        }
        std::unreachable();
    }

    void abort() {
        RT::get_runtime().abort(task_id);
        auto t = worker::get_worker_from_task_id(task_id).sc.get_task(task_id);
        if (!t.is_inline)
            RT::get_runtime().awake(task_id);
    }

    future_base() = default;

    future_base(corohandle task, size_t task_id)
            : task(task)
            , task_id(task_id)
            , none(false) {
        task.promise().awaiter = this;
        task.promise().awaiter_sem.release();
        RT::get_runtime().awake(task_id);
    }

    ~future_base() {
        if (none)
            return;

        if (!promise_returned)
            task.promise().awaiter = nullptr;
    }

    T &&retval_move_out() {
        if (none)
            throw std::runtime_error("[ASCO] retval_move_out(): from a none future object.");

        none = true;
        return std::move(retval);
    }

private:
    corohandle task;
    size_t task_id;

    T retval;
    std::exception_ptr e;
    bool promise_returned{false};

    bool none{true};
};

template<typename T, typename R = RT>
    requires is_move_secure_v<T> && is_runtime<R>
using future = future_base<T, false, false, R>;

template<typename T, typename R = RT>
    requires is_move_secure_v<T> && is_runtime<R>
using future_inline = future_base<T, true, false, R>;

template<typename T, typename R = RT>
    requires is_move_secure_v<T> && is_runtime<R>
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

};  // namespace asco

#include <asco/futures.h>

#endif
