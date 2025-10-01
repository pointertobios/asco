# Barrier

Header: `<asco/sync/barrier.h>`

Call `arrive()` to obtain a wait token; then `co_await token.wait()` to wait for the rest of the participants of the current generation.

`token::wait()` returns `future_inline<void>` (must be `co_await`ed).

Call `co_await bar.all_arrived()` to wait until the entire current generation has arrived at the barrier (i.e. after all `arrive()` calls for that generation complete).

```cpp
constexpr size_t NUM_THREADS = 5;

future<void> worker(asco::sync::barrier<NUM_THREADS> &bar, size_t id) {
    co_await bar.arrive().wait();
    co_return;
}

future<int> async_main() {
    asco::sync::barrier<NUM_THREADS> bar;
    for (size_t i = 0; i < NUM_THREADS; ++i) {
        worker(bar, i + 1);
    }
    co_await bar.all_arrived();
    co_return 0;
}
```
