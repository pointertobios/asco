#ifndef ASCO_FUTURE_H
#define ASCO_FUTURE_H

#include <coroutine>

#include <asco/utils/utils.h>

namespace asco {

template<typename T>
requires is_move_secure_v<T>
class future_awaiter {
public:
    explicit future_awaiter(bool& ready) : ready(ready) {}

    bool await_ready() { return ready; }

    void await_suspend(std::coroutine_handle<future_promise<T>> handle) {
        ready = false;
    }

    T await_resume() { return result; }

private:
    bool& ready;
    T result;
};

template<typename T>
requires is_move_secure_v<T>
struct future_promise {
    using coro_handle = std::coroutine_handle<future_promise<T>>;

    auto await_transform(future<T>&& coro) { return future_awaiter<T>(!ready); }

private:
    bool ready{true};
};

template <typename T>
requires (std::is_move_assignable_v<T> && std::is_move_constructible_v<T>)
struct future {
    using promise_type = future_promise;
    explicit future() {}
    explicit future(const future&) = delete;
    explicit future(const future&&) = delete;

private:
    T result;
};

}

#endif