// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <atomic>
#include <coroutine>

#include <asco/core/runtime.h>
#include <asco/utils/pubusing.h>
#include <asco/utils/concepts.h>

#if defined(_MSC_VER) && !defined(__clang__)
    #error "[ASCO] Compile with clang-cl instead of MSVC"
#endif

#ifndef SET_RUNTIME
    using RT = asco::runtime;
#endif

namespace asco {

struct __future_void {};

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

    T retval;

    struct promise_type {
        // Alway put this at the first,
        // so that we can authenticate the coroutine_handle type when we
        // reinterpret it from raw pointer.
        size_t future_type_hash;

        std::binary_semaphore returned{0};
        size_t task_id;
        future_base *awaiter{nullptr};
        std::binary_semaphore awaiter_sem{0};

        atomic_bool aborted{false};

        std::mutex suspend_mutex;

        std::coroutine_handle<> caller_task;

        __coro_local_frame *inline_swap_frame;

        std::exception_ptr e;

        future_base<T, Inline, Blocking> get_return_object() {
            __coro_local_frame *curr_clframe;
            try {
                curr_clframe = worker::get_worker()->current_task().coro_local_frame;
            } catch (...) {
                curr_clframe = nullptr;
            }

            auto coro = corohandle::from_promise(*this);
            if constexpr (!Inline) {
                if constexpr (Blocking) {
                    task_id = RT::get_runtime()->spawn_blocking(coro, curr_clframe);
                } else {
                    task_id = RT::get_runtime()->spawn(coro, curr_clframe);
                }
            } else {
                using state = worker::scheduler::task_control::__control_state;
                auto worker = worker::get_worker();

                if constexpr (Blocking) {
                    auto t = RT::get_runtime()->to_task(coro, Blocking, curr_clframe);
                    t.is_inline = true;
                    task_id = t.id;
                    worker->sc.push_task(t, state::suspending);
                } else {
                    auto t = RT::get_runtime()->to_task(coro, Blocking, curr_clframe);
                    t.is_inline = true;
                    task_id = t.id;
                    worker->sc.push_task(t, state::suspending);
                }

                worker::insert_task_map(task_id, worker);
            }
            future_type_hash = type_hash<future_base<T, Inline, Blocking>>();
            return future_base<T, Inline, Blocking>(coro, task_id);
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            awaiter_sem.acquire();
            awaiter->retval = std::move(val);
            returned.release();
        }

        // The inline future will return the caller_resumer to symmatrical transform
        // to the caller task.
        //
        // To support abortable task, the logic of suspent current task and awake caller
        // task execute here instrad of in final_suspend().
        // The current task (if returns future<T>/future_blocking<T>/future_inline<T>) can create a struct like this:
        // ```c++
        // struct __restorer {
        //     T ret;
        //     ~__restorer() noexcept {
        //         if (futures::aborted<future<T>>()) {
        //             // recover logic
        //         }
        //     }
        // } restorer{};
        // ```
        // Then after the task ended, you can use:
        // ```c++
        // co_return std::move(restorer.res);
        // ```
        // to let this task execute recover logic.
        // At the same time the caller coroutine will continue without waiting for the current task final_suspend().
        auto final_suspend() noexcept {
            if constexpr (!Inline) {

                while (!task_id);
                std::lock_guard lk{suspend_mutex};
                auto rt = RT::get_runtime();
                if (caller_task) {
                    auto id = rt->task_id_from_corohandle(caller_task);
                    rt->awake(id);
                }

                // When catch an exception, it means the task successfully destroyed.
                // Just ignore.
                try {
                    rt->suspend(task_id);
                } catch (...) {}

                return std::suspend_always{};
            } else {
                struct caller_resumer {
                    std::coroutine_handle<> caller_task;

                    bool await_ready() noexcept { return false; }

                    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) noexcept {
                        auto rt = RT::get_runtime();
                        auto worker = worker::get_worker();

                        auto id = rt->task_id_from_corohandle(caller_task);
                        worker->running_task.push(worker->sc.get_task(id));

                        // Do not awake this task, but resume it inplace.
                        // If there is no any suspend point, it will be then never resumed
                        // after final_suspend().
                        // or it will be awaken after first co_await.
                        return caller_task;
                    }

                    void await_resume() noexcept {}
                };

                auto rt = RT::get_runtime();
                auto worker = worker::get_worker();

                rt->remove_task_map(corohandle::from_promise(*this).address());
                worker->remove_task_map(task_id);

                return caller_resumer{caller_task};
            }
        }

        void unhandled_exception() {
            e = std::current_exception();
        }
    };

    bool await_ready() {
        auto res = task.promise().returned.try_acquire();
        if (res)
            task.promise().returned.release();
        return res;
    }

    // The inline future will return current task corohandle to symmatrical transform
    // to the callee task.
    auto await_suspend(std::coroutine_handle<> handle) {
        if constexpr (!Inline) {
            std::lock_guard lk{task.promise().suspend_mutex};
            if (!task.promise().returned.try_acquire()) {

                task.promise().caller_task = handle;
                auto rt = RT::get_runtime();
                auto id = rt->task_id_from_corohandle(handle);
                rt->suspend(id);
                return true;

            } else {
                task.promise().returned.release();
                return false;
            }
        } else {
            task.promise().caller_task = handle;
            auto worker = worker::get_worker();
            auto rt = RT::get_runtime();
            auto id = rt->task_id_from_corohandle(handle);
            rt->suspend(id);

            // Do not awake this task, but resume it inplace.
            // If there is no any suspend point, it will be then never resumed
            // after final_suspend().
            // or it will be awaken after first co_await.
            worker->running_task.push(worker->sc.get_task(task_id)); // Correct running task
            return task;
        }
    }

    T await_resume() { return std::move(retval); }

    T await() {
        if (RT::__worker::in_worker())
            throw std::runtime_error("[ASCO] Cannot use synchronized await in asco::runtime");

        if constexpr (!Inline) {

            if (task.promise().returned.try_acquire())
                return std::move(retval);

            RT::get_runtime()->register_sync_awaiter(task_id);
            RT::__worker::get_worker_from_task_id(task_id)->sync_awaiters.at(task_id).acquire();

            return std::move(retval);

        } else {
            static_assert(false, "[ASCO] future_inline<T> cannot be awaited in synchronized context.");
        }
    }

    void abort() {
        task.promise().aborted.store(true, morder::acq_rel);
    }

    future_base(corohandle task, size_t task_id)
        : task(task), task_id(task_id) {
        task.promise().awaiter = this;
        task.promise().awaiter_sem.release();
    }

private:
    corohandle task;
    size_t task_id;
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

using future_void = future<__future_void>;
using future_void_inline = future_inline<__future_void>;
using future_void_core = future_core<__future_void>;

using runtime_initializer_t = std::optional<std::function<runtime *()>>;

}; // namespace asco

#endif
