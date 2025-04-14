// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <atomic>
#include <iostream>
#include <coroutine>

#include <asco/runtime.h>
#include <asco/utils/utils.h>

using std::cout, std::endl;

#ifndef SET_RUNTIME
    using RT = asco::runtime;
#endif

namespace asco {

template<typename R = RT>
requires is_runtime<R>
R *get_runtime() {
    return R::get_runtime();
}

template<typename T, bool Inline, bool Blocking, typename R = RT>
requires is_move_secure_v<T> && is_runtime<R>
struct future_base {
    static_assert(!std::is_void_v<T>, "Use asco::future_void instead.");
    static_assert(!Inline || !Blocking, "Inline coroutine cannot be blocking.");

    struct promise_type;
    using corohandle = std::coroutine_handle<promise_type>;

    struct promise_type {
        T retval;
        std::atomic_bool returned{false};
        volatile size_t task_id;

        std::mutex suspend_mutex;

        std::coroutine_handle<> caller_task;

        std::exception_ptr e;

        future_base<T, Inline, Blocking> get_return_object() {
            // std::cout << std::format("future_base::promise_type::get_return_object()\n");
            auto coro = corohandle::from_promise(*this);
            if constexpr (!Inline) {
                if constexpr (Blocking) {
                    this->task_id = RT::get_runtime()->spawn_blocking(coro);
                } else {
                    this->task_id = RT::get_runtime()->spawn(coro);
                }
            }
            return future_base<T, Inline, Blocking>(coro, task_id);
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            // std::cout << std::format("future_base::promise_type::return_value({})\n", val);
            retval = std::move(val);
            returned = true;
        }

        auto final_suspend() noexcept {
            // std::cout << std::format("future_base::promise_type::final_suspend()\n");
            if constexpr (!Inline) {
                while (!task_id);
                std::lock_guard lk{suspend_mutex};
                auto rt = RT::get_runtime();
                if (caller_task) {
                    auto id = rt->task_id_from_corohandle(caller_task);
                    rt->awake(id);
                }
                rt->suspend(task_id);
            }
            return std::suspend_always{};
        }

        void unhandled_exception() {
            // std::cout << std::format("future_base::promise_type::unhandled_exception()\n");
            e = std::current_exception();
        }
    };

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        // std::cout << std::format("future_base::await_suspend({})\n", handle.address());
        if constexpr (!Inline) {
            std::lock_guard lk{task.promise().suspend_mutex};
            task.promise().caller_task = handle;
            if (!task.promise().returned) {
                auto rt = RT::get_runtime();
                auto id = rt->task_id_from_corohandle(handle);
                rt->suspend(id);
            } else {
                task.promise().caller_task = 0;
            }
        } else {
            task.resume();
        }
        return true;
    }

    T await_resume() {
        // std::cout << std::format("future_base::await_resume() -> {}\n", task.promise().retval);
        task.promise().caller_task = 0;
        return task.promise().retval;
    }

    T await() {
        try {
            R::__worker::get_worker();
        } catch (...) {
            if constexpr (!Inline) {
                if (task.promise().returned)
                    return std::move(task.promise().retval);
                RT::get_runtime()->register_sync_awaiter(task_id).await();
                return std::move(task.promise().retval);
            } else {
                static_assert(false, "[ASCO] Inline future<T> cannot be awaited in synchronized context.");
            }
        }
        throw std::runtime_error("[ASCO] Cannot use synchronized await in asco::runtime");
    }

    future_base(corohandle task, size_t task_id)
        : task(task), task_id(task_id) {}

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
using future_blocking = future_base<T, false, true, R>;

struct __future_void {};

using future_void = future<__future_void>;
using future_void_inline = future_inline<__future_void>;
using future_void_blocking = future_blocking<__future_void>;

#define asco_main                                                   \
    using asco::future, asco::future_inline, asco::future_blocking; \
    asco::future<int> async_main();                                 \
    int main(int argc, const char **argv, const char **env) {       \
        asco::runtime rt;                                           \
        asco::runtime::sys::set_args(argc, argv);                   \
        asco::runtime::sys::set_env(const_cast<char **>(env));      \
        return async_main().await();                                \
    }

};

#endif
