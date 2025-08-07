# 定时器

导入 `<asco/time/interval.h>` 头文件使用。

定时器每超过固定时间产生一次 `tick` 。

定时器无法保证完全精确，其精度受操作系统调度的影响。

调用方法 `asco::interval::tick()` 并 `co_await` ，协程挂起至定时器到达唤醒点，或当时间过短直接忙等待至唤醒点。

使用例：

```c++
#include <asco/future.h>
#include <asco/time/interval.h>

using asco::future;
using asco::interval;
using namespace std::chrono_literals;

future<void> foo() {
    interval in(1s);
    for (int i = 0; i < 10; i++) {
        co_await in.tick();
        std::cout << "tick foo" << std::endl;
    }
    co_return;
}

future<int> async_main() {
    auto task = foo();
    interval in(500ms);
    for (int i = 0; i < 10; i++) {
        co_await in.tick();
        std::cout << "tick async_main" << std::endl;
    }
    co_return 0;
}
```
