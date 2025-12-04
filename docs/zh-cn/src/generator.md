# 协程生成器 `generator<T>`

`generator<T>` 是 ASCO 中基于 C++ 协程实现的懒序列。它继承自 `future_base`，因此可以像 `future` 一样被调度，但每次 `co_yield` 会把一个元素推入内部队列，供消费端按需提取。

## 设计概览

- **逐项产出**：生成器协程通过 `co_yield` 把值送入 `continuous_queue`，调用方每次等待一个元素。
- **基于 `future`**：`generator<T>` 依旧是异步任务，`operator()` 返回另一个 `future<std::optional<T>>`，因此可以在任何 `co_await` 环境里消费。
- **跨线程安全**：内部通过信号量与无锁队列保证并发安全，能够在不同工作线程间生成与消费。
- **结束语义**：当协程 `co_return` 或 `co_yield` 通道关闭时，`operator()` 会返回 `std::nullopt`，并且 `operator bool()` 变为 `false`。

## 基本流程

```cpp
#include <asco/generator.h>
#include <asco/future.h>

using asco::future;
using asco::generator;

// 生产 1..n 的序列
generator<int> gen_count(int n) {
    for (int i = 1; i <= n; ++i) {
        co_yield i;
    }
    co_return;
}

future<void> consume(generator<int>& g) {
    int sum = 0;
    while (auto i = co_await g()) { // 每次等待一个元素
        sum += *i;
    }
    // generator 被耗尽或停止时跳出循环
    co_return;
}
```

调用 `g()` 返回一个可等待的 future，获取到 `std::optional<T>`。当生成器结束时返回 `std::nullopt`，表明序列已经耗尽。

## 组合消费 (`operator|`)

`generator` 支持通过 `operator|` 与异步函数拼接，形成更简洁的管道式写法。`consumer` 需要是一个返回 `future` 的函数，并接受生成器作为参数。

```cpp
future<int> consume_sum(generator<int>& g) {
    int sum = 0;
    while (auto item = co_await g()) {
        sum += *item;
    }
    co_return sum;
}

future<void> async_main() {
    auto g = gen_count(5);

    // 传统写法
    auto sum1 = co_await consume_sum(g);

    // 管道写法，会自动调用 co_invoke(consumer, generator)
    auto g2 = gen_count(5);
    auto sum2 = co_await (g2 | consume_sum);

    co_return;
}
```

该操作符同样支持右值生成器，内部会移动到消费函数中：

```cpp
auto total = co_await (gen_count(5) | [](auto gen) -> future<int> {
    int sum = 0;
    while (auto item = co_await gen()) {
        sum += *item;
    }
    co_return sum;
});

// 消费函数还可以返回新的 generator/generator_core，实现链式加工
auto doubled = co_await (gen_count(3) | [](auto gen) -> generator<int> {
    while (auto item = co_await gen()) {
        co_yield *item * 2;
    }
    co_return;
});
```

## 异常与停止

- 生成器内部抛出的异常会在下一次 `co_await generator()` 时重新抛出，调用方可以像处理普通异步任务一样捕获。
- 调用生成器的 `operator bool()` 可以检测它是否仍然可继续产出值。
- 一旦协程结束或调用方显式停止（例如销毁生成器），内部队列会关闭，后续获取将立即返回 `std::nullopt`。

## 与运行时的关系

`generator<T>` 的调度由 ASCO 运行时处理。和 `future` 一样，需要在 `async_main` 或其他运行时任务中 `co_await`，以确保协程被调度执行。若要在核心 runtime 上下文内使用，可以改用 `generator_core<T>`，它会绑定到核心执行器。
