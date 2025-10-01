# Condition Variable

Header: `<asco/sync/condition_variable.h>`

Use `notify_one()` to wake one waiting coroutine, `notify_all()` to wake all.

Use `wait(predicate)`; returns `future_inline<void>` (must be `co_await`ed). Unlike `std::condition_variable::wait`, this variant only accepts a predicate; atomicity of the predicate evaluation must be ensured by the caller (e.g. atomics instead of a separate lock).

Entering the wait-queue and suspending the coroutine are atomic with respect to each other in this implementation.

```cpp
future<int> async_main() {
    condition_variable decl_local(cv);
    atomic_bool decl_local(flag, new atomic_bool{false});
    auto task = []() -> future<void> {
        condition_variable coro_local(cv);
        atomic_bool coro_local(flag);
        co_await cv.wait([&flag]{ return flag.load(); });
        std::cout << "cv notified" << std::endl;
        co_return;
    }();
    co_await this_coro::sleep_for(1s);
    flag.store(true);
    cv.notify_one();
    co_await task;
    co_return 0;
}
```
