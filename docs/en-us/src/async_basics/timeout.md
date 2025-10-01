# `timeout(T, F)`

Header: `<asco/time/timeout.h>`

`timeout` returns a `future_inline<std::optional<T>>` (an inline coroutine). You must `co_await` it to start execution.

`T` is deduced from the return type of the second argument (an asynchronous no-arg callable). If the inner async operation finishes before the duration, its result is wrapped in `std::optional`. If the duration elapses first, `std::nullopt` is returned.

The inner asynchronous function must be abortable (see abort semantics).

This wrapper itself is unabortable.

## Example

```cpp
#include <iostream>

#include <asco/future.h>
#include <asco/time/timeout.h>

using asco::future;
using asco::timeout; using asco::interval;
using namespace std::chrono_literals;

future<int> async_main() {
    auto res = co_await timeout(1s, [] -> future_inline<void> {
        interval in{2s};
        std::cout << "interval start\n";
        co_await in.tick();
        if (asco::this_coro::aborted()) {
            std::cout << "timeout aborted\n";
        } else {
            std::cout << "interval 2s\n";
        }
        co_return;
    });
    if (!res)
        std::cout << "timeout\n";
    else
        std::cout << "not timeout\n";
    co_return 0;
}
```

## Notes

- Abortable inner task + unabortable wrapper clarifies cancellation boundary.
- When designing long-running inner tasks, insert periodic abort checks (`asco::this_coro::aborted()`).
