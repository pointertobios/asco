# timeout(Ti, F)

导入 `<asco/time/timeout.h>` 头文件使用。

`timeout` 函数返回一个 `future_inline<std::optional<T>>` ，此类型为内联协程，必须 `co_await` 使其开始执行。

类型 `T` 从第二个参数类型推导。要求第二个参数必须是无形参的异步函数。使用其实际返回值类型作为 `T` 。

若异步函数超时，返回 `std::nullopt` ，否则返回异步函数的返回值。

传入的异步函数需要实现可打断特性[^1]。

此函数***不是***可打断协程。

```c++
#include <iostream>

#include <asco/future.h>
#include <asco/time/timeout.h>

using asco::future;
using asco::timeout, asco::interval;

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

[^1]: [可打断特性](../future.md#可打断协程)
