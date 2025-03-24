#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

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
    struct promise_type;
    using corohandle = std::coroutine_handle<promise_type>;

    struct promise_type {
        T value;
        std::exception_ptr eptr;
        
        size_t continuation_worker_id = static_cast<size_t>(-1);
        uint64_t continuation_task_id = 0;

        future<T> get_return_object() {
            cout << "promise_type::get_return_object" << endl;
            auto h = corohandle::from_promise(*this);
            auto worker_id = R::get_runtime()->spawn(h);
            return future<T>(h, worker_id);
        }
        std::suspend_always initial_suspend() noexcept {
            cout << "promise_type::initial_suspend" << endl;
            return {};
        }

        struct final_awaiter {
            bool await_ready() noexcept {
                cout << "final_awaiter::await_ready" << endl;
                return false;
            }
            void await_suspend(corohandle h) noexcept {
                cout << "final_awaiter::await_suspend" << endl;
                auto& promise = h.promise();
                if (promise.continuation_worker_id != static_cast<size_t>(-1)) {
                    R::get_runtime()->awake(promise.continuation_worker_id);
                }
            }
            void await_resume() noexcept {
                cout << "final_awaiter::await_resume" << endl;
            }
        };

        final_awaiter final_suspend() noexcept {
            cout << "promise_type::final_suspend" << endl;
            return {};
        }

        void unhandled_exception() {
            cout << "promise_type::unhandled_exception" << endl;
            eptr = std::current_exception();
        }
        void return_value(T v) {
            cout << "promise_type::return_value" << endl;
            value = std::move(v);
        }
    };

    explicit future(corohandle h, size_t worker_id)
        : coro_handle(h), owner_worker_id(worker_id) {}

    struct awaiter {
        corohandle callee_handle;
        size_t caller_worker_id;

        bool await_ready() noexcept {
            cout << "awaiter::await_ready" << endl;
            return false;
        }
        void await_suspend(std::coroutine_handle<> caller) noexcept {
            cout << "awaiter::await_suspend" << endl;

            callee_handle.promise().continuation_worker_id = caller_worker_id;
            callee_handle.promise().continuation_task_id = 
                reinterpret_cast<uint64_t>(caller.address());

            RT::get_runtime()->awake(callee_handle.promise().owner_worker_id);
        }
        T await_resume() { 
            cout << "awaiter::await_resume" << endl;
            if (callee_handle.promise().eptr) {
                std::rethrow_exception(callee_handle.promise().eptr);
            }
            return std::move(callee_handle.promise().value);
        }
    };
    awaiter operator co_await() { 
        return awaiter{coro_handle, owner_worker_id};
    }

private:
    corohandle coro_handle;
    size_t owner_worker_id;
};

};

#endif