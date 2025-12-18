# Join Set 聚合器

`asco:::join_set<T = void>` 提供一个轻量的“任务收集器”，用于在多个异步计算完成后，以生成器形式逐个取回结果。它内部使用 `channel<T>` 管道按完成顺序输送值，适合将若干协程并发运行、再统一遍历其结果。

- 头文件：`#include <asco/join_set.h>`
- 依赖组件：`future`、`generator`、`channel`

## 基础用法

典型流程：

1. 创建 `join_set<T>` 实例。
2. 调用 `spawn(fn)` 注册若干异步任务。`fn` 必须结果类型统一为 `T`。
3. 调用 `join()` 得到 `generator<T>`，随后通过 `co_await` 按完成顺序消费结果。

示例：

```cpp
#include <asco/future.h>
#include <asco/join_set.h>
#include <asco/time/sleep.h>
using namespace asco;

future<int> async_main() {
    base::join_set<int> set;

    for (int i = 0; i < 3; ++i) {
        set.spawn([i]() -> future<int> {
               co_await sleep_for(std::chrono::milliseconds{10 * (i + 1)});
               co_return i;
           })
            .ignore();
    }

    auto results = set.join();
    while (auto value = co_await results()) {
        std::println("result = {}", *value);
    }
    co_return 0;
}
```

## 接口概览

- `explicit join_set(Creator ctor)`：允许自定义底层 `channel` 的创建器；默认使用 `continuous_queue`。
- `future_spawn<void> spawn(Fn &&fn)`：启动一个异步任务，将其结果推入内部 `channel`。`Fn` 必须返回 `future<T>` 或 `future_spawn<T>`，否则编译失败。
- `generator<T> join()`：停止接受新任务（内部关闭 `sender`），并返回一个生成器。生成器被迭代完毕后会自动退出。

## 行为与保证

- 结果顺序等于任务实际完成顺序；如果多个任务同时完成，顺序取决于 `channel` 的排队次序。
- 任务抛出异常时会沿 `future` 传播；可以在调用处使用 `.ignore(on_exception)` 或额外捕获。

## 使用建议

- 若任务很多，建议结合 `std::for_each` / `ranges` 批量调用 `spawn`，并尽早 `.ignore()` 避免遗漏。
- 如果需要自定义任务队列特性（例如环形缓冲区），可以提供自定义 `Creator` 参数构造 `join_set`。
