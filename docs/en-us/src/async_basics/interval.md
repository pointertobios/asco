# Interval Timer

Header: `<asco/time/interval.h>`

An interval produces a `tick` each time a fixed duration elapses.
Precision is not guaranteed; it depends on OS scheduling. If the remaining wait is very short the implementation may busy-wait until the wake point.

Call `co_await interval.tick()` to suspend until the next scheduled tick.

## Example

```cpp
#include <asco/future.h>
#include <asco/time/interval.h>

using asco::future;
using asco::interval;
using namespace std::chrono_literals;

future<void> foo() {
    interval in(1s);
    for (int i = 0; i < 10; ++i) {
        co_await in.tick();
        std::cout << "tick foo" << std::endl;
    }
    co_return;
}

future<int> async_main() {
    auto task = foo();
    interval in(500ms);
    for (int i = 0; i < 10; ++i) {
        co_await in.tick();
        std::cout << "tick async_main" << std::endl;
    }
    co_return 0;
}
```

## Notes

- Not real-time precise.
- Use shorter intervals judiciously; busy-wait fallback can increase CPU usage.
