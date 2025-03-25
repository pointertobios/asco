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

        std::coroutine_handle<> caller_task;

        std::exception_ptr e;

        future<T> get_return_object() {
            auto coro = corohandle::from_promise(*this);
            auto task_id = RT::get_runtime()->spawn(coro);
            this->task_id = task_id;
            return future<T>(coro, task_id);
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value(T val) {
            retval = std::move(val);
            returned = true;
        }

        std::suspend_always final_suspend() noexcept {
            if (caller_task == 0)
                return {};
            auto rt = RT::get_runtime();
            auto id = rt->task_id_from_corohandle(caller_task);
            rt->suspend(task_id);
            rt->awake(id);
            return {};
        }

        void unhandled_exception() {
            e = std::current_exception();
        }
    };

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        task.promise().caller_task = handle;
        auto rt = RT::get_runtime();
        auto id = rt->task_id_from_corohandle(handle);
        rt->suspend(id);
        return true;
    }

    T await_resume() {
        return task.promise().retval;
    }

    T await() {
        if (task.promise().returned)
            return std::move(task.promise().retval);
        RT::get_runtime()->register_sync_awaiter(task_id).await();
        return std::move(task.promise().retval);
    }

    future(corohandle task, size_t task_id)
        : task(task), task_id(task_id) {}

private:
    corohandle task;
    size_t task_id;
};

struct __future_void {};

using future_void = future<__future_void>;

#define asco_main                   \
    asco::future_void async_main(); \
    int main()                      \
    {                               \
        asco::runtime rt;           \
        async_main().await();       \
        return 0;                   \
    }


};

#endif