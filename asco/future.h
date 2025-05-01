// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <atomic>
#include <coroutine>
#include <iostream>
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
    bool aborted{false};

    struct promise_type {
        // Alway put this at the first, so that we can authenticate the coroutine_handle type when we
        // reinterpret it from raw pointer.
        size_t future_type_hash{type_hash<future_base<T, Inline, Blocking>>()};

        std::binary_semaphore returned{0};

        size_t task_id;
        future_base *awaiter{nullptr};
        std::binary_semaphore awaiter_sem{0};

        std::coroutine_handle<> caller_task;
        size_t caller_task_id{0};

        std::exception_ptr e;

        void *operator new(size_t n) noexcept {
            size_t *p = reinterpret_cast<size_t *>(::operator new(n + sizeof(size_t)));
            *p = n;
            return p + 1;
        }

        void operator delete(void *p) noexcept {
            size_t *q = reinterpret_cast<size_t *>(p) - 1;
            ::operator delete(q);
        }

        template<size_t Hash>
        static bool group_local_exists() {
            if (!RT::__worker::in_worker())
                return false;
            auto curid = RT::__worker::get_worker()->current_task_id();
            if (!RT::get_runtime()->in_group(curid))
                return false;
            return RT::get_runtime()->group(curid)->var_exists<Hash>();
        }

        template<size_t Hash>
        static bool coro_local_exists() {
            return RT::__worker::get_worker()->current_task().coro_local_frame->var_exists<Hash>();
        }

        corohandle spawn(__coro_local_frame *curr_clframe) {
            auto coro = corohandle::from_promise(*this);
            if constexpr (!Inline) {
                if constexpr (Blocking)
                    task_id = RT::get_runtime()->spawn_blocking(coro, curr_clframe);
                else
                    task_id = RT::get_runtime()->spawn(coro, curr_clframe);
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

                if (worker->is_calculator)
                    RT::get_runtime()->inc_calcu_load();
                else
                    RT::get_runtime()->inc_io_load();

                worker::insert_task_map(task_id, worker);
            }

            if (group_local_exists<__consteval_str_hash("__asco_select_sem__")>()) {
                auto currid = RT::__worker::get_worker()->current_task_id();
                RT::get_runtime()->join_task_to_group(task_id, currid);
            }

            return coro;
        }

        future_base<T, Inline, Blocking> get_return_object() {
            __coro_local_frame *curr_clframe = nullptr;
            if (RT::__worker::in_worker()) {
                curr_clframe = RT::__worker::get_worker()->current_task().coro_local_frame;
            }
            auto coro = spawn(curr_clframe);
            return future_base<T, Inline, Blocking>(coro, task_id);
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            if (group_local_exists<__consteval_str_hash("__asco_select_sem__")>()) {
                std::binary_semaphore group_local(__asco_select_sem__);
                if (!__asco_select_sem__.try_acquire()) {
                    awaiter->aborted = true;
                    return;
                }
                atomic_size_t group_local(__asco_select_promise_countdown__);
                auto rest = __asco_select_promise_countdown__.fetch_sub(1, morder::release);
                if (rest == 1) {
                    del_glocal("__asco_select_sem__");
                    del_glocal("__asco_select_promise_countdown__");
                }
            }

            awaiter_sem.acquire();
            awaiter->retval = std::move(val);
            awaiter_sem.release();
            returned.release();
        }

        // The inline future will return the caller_resumer to symmatrical transform to the caller task.
        auto final_suspend() noexcept {
            auto rt = RT::get_runtime();

            if (group_local_exists<__consteval_str_hash("__asco_select_sem__")>()) {
                rt->exit_group(task_id);
                if (caller_task_id)
                    rt->exit_group(caller_task_id);
                if (awaiter->aborted) {
                    if (caller_task_id) {
                        auto w = RT::__worker::get_worker_from_task_id(caller_task_id);
                        rt->remove_task_map(caller_task.address());
                        w->remove_task_map(caller_task_id);
                        if (w->is_calculator)
                            rt->dec_calcu_load();
                        else
                            rt->dec_io_load();
                        w->sc.destroy(caller_task_id, true);
                    }
                    return std::suspend_always{};
                }
            }

            if constexpr (!Inline) {
                while (!task_id);
                if (caller_task_id)
                    rt->awake(caller_task_id);

                // When catch an exception, it means the task successfully destroyed.
                // Just ignore.
                try {
                    rt->suspend(task_id);
                } catch (...) {
                }

                return std::suspend_always{};
            } else {
                auto worker = worker::get_worker();

                try {
                    rt->suspend(task_id);
                } catch (...) {
                }
                rt->remove_task_map(corohandle::from_promise(*this).address());
                worker->remove_task_map(task_id);
                rt->awake(caller_task_id);

                return std::suspend_always{};
            }
        }

        void unhandled_exception() {
            awaiter_sem.acquire();
            e = std::current_exception();
            awaiter_sem.release();
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
            if (!task.promise().returned.try_acquire()) {
                auto rt = RT::get_runtime();
                auto id = rt->task_id_from_corohandle(handle);
                task.promise().caller_task = handle;
                task.promise().caller_task_id = id;
                rt->suspend(id);
                return true;
            } else {
                task.promise().returned.release();
                return false;
            }
        } else {
            auto worker = worker::get_worker();
            auto rt = RT::get_runtime();
            auto id = rt->task_id_from_corohandle(handle);
            task.promise().caller_task = handle;
            task.promise().caller_task_id = id;
            rt->suspend(id);

            worker->running_task.push(worker->sc.get_task(task_id));
            return task;
        }
    }

    T await_resume() {
        if (task.promise().e)
            std::rethrow_exception(task.promise().e);
        return std::move(retval);
    }

    T await() {
        if (RT::__worker::in_worker())
            throw std::runtime_error("[ASCO] Cannot use synchronized await in asco::runtime");

        if constexpr (!Inline) {
            if (task.promise().returned.try_acquire())
                return std::move(retval);

            RT::get_runtime()->register_sync_awaiter(task_id);
            RT::__worker::get_worker_from_task_id(task_id)->sc.get_sync_awaiter(task_id).acquire();

            if (task.promise().e)
                std::rethrow_exception(task.promise().e);

            return std::move(retval);

        } else {
            static_assert(false, "[ASCO] future_inline<T> cannot be awaited in synchronized context.");
        }
    }

    void abort() { aborted = true; }

    future_base() {}

    future_base(corohandle task, size_t task_id)
            : task(task)
            , task_id(task_id)
            , none(false) {
        task.promise().awaiter = this;
        task.promise().awaiter_sem.release();
        RT::get_runtime()->awake(task_id);
    }

private:
    corohandle task;
    size_t task_id;
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

using future_void = future<__future_void>;
using future_void_inline = future_inline<__future_void>;
using future_void_core = future_core<__future_void>;

using runtime_initializer_t = std::optional<std::function<runtime *()>>;

};  // namespace asco

#endif
