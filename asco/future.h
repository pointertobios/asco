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

template<typename T, typename R = RT>
requires is_move_secure_v<T> && is_runtime<R>
struct future {
    static_assert(!std::is_void_v<T>, "Use asco::future_void instead.");

    struct promise_type;
    using corohandle = std::coroutine_handle<promise_type>;

    struct promise_type {
        T retval;
        std::atomic_bool returned{false};
        size_t task_id;

        std::mutex suspend_mutex;

        std::coroutine_handle<> caller_task;

        std::exception_ptr e;

        future<T> get_return_object() {
            auto coro = corohandle::from_promise(*this);
            auto task_id = RT::get_runtime()->spawn(coro);
            this->task_id = task_id;
            // std::cout << std::format("Task {} get_return_object().\n", task_id);
            return future<T>(coro, task_id);
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            // std::cout << std::format("Task {} return_value(): returned {}.\n", task_id, val);
            retval = std::move(val);
            returned = true;
        }

        std::suspend_always final_suspend() noexcept {
            std::lock_guard lk{suspend_mutex};
            // std::cout << std::format("Task {} final_suspend().\n", task_id);
            auto rt = RT::get_runtime();
            if (caller_task) {
                auto id = rt->task_id_from_corohandle(caller_task);
                rt->awake(id);
            }
            rt->suspend(task_id);
            return {};
        }

        void unhandled_exception() {
            // std::cout << std::format("Task {} unhandled_exception().\n", task_id);
            e = std::current_exception();
        }
    };

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        std::lock_guard lk{task.promise().suspend_mutex};
        // std::cout << std::format("Task {} await_suspend().\n", task_id);
        if (!task.promise().returned) {
            auto rt = RT::get_runtime();
            auto id = rt->task_id_from_corohandle(handle);
            rt->suspend(id);
        }
        task.promise().caller_task = handle;
        return true;
    }

    T await_resume() {
        // std::cout << std::format("Task {} await_resume().\n", task_id);
        return task.promise().retval;
    }

    T await() {
        if (task.promise().returned)
            return std::move(task.promise().retval);
        RT::get_runtime()->register_sync_awaiter(task_id).await();
        return std::move(task.promise().retval);
    }

    future(corohandle task, size_t task_id)
        : task(task), task_id(task_id) {
        RT::get_runtime()->awake(task_id);
    }

private:
    corohandle task;
    size_t task_id;
};

struct __future_void {};

using future_void = future<__future_void>;

#define asco_main                                               \
    asco::future<int> async_main();                             \
    int main(int argc, const char **argv, const char **env) {   \
        asco::runtime rt;                                       \
        asco::runtime::sys::set_args(argc, argv);               \
        asco::runtime::sys::set_env(const_cast<char **>(env));  \
        return async_main().await();                            \
    }

};

#endif
