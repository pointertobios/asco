# `future<T>` 与异步函数

ASCO 将“异步函数调用”和“异步任务”明确区分为两类对象：`future<T>` 与 `join_handle<T>`。

- `future<T>`：异步函数的返回类型，表示一次异步调用的结果；`co_await` 该对象会执行对应协程并得到 `T`。
- `join_handle<T>`：由运行时调度的任务句柄；可 `co_await` 以等待任务完成，或 `detach()` 以放弃 join。

---

## 1. `future<T>`：异步函数调用的惰性返回对象

返回 `future<T>` 的可调用对象满足 `asco::async_function` 概念，可视为 ASCO 语义下的异步函数。

```cpp
#include <asco/future.h>
using namespace asco;

future<int> add_one(int x) {
    co_return x + 1;
}
```

### 1.1 惰性执行：调用不执行，`co_await` 才执行

`future<T>` 是惰性对象。因此：

- 调用 `add_one(41)` 仅构造 `future<int>`（协程句柄处于初始挂起态）。
- 对该对象执行 `co_await` 时，协程才开始运行。

```cpp
future<int> example() {
    auto f = add_one(41);  // 仅构造 future
    int v = co_await f;    // 协程在此处开始执行
    co_return v;
}
```

### 1.2 等待语义：内联执行（非并发）

`co_await future<T>` 会在当前执行流中推进被等待的协程执行，并得到 `T`。它表达的是“等待一次异步调用的结果”，而不是“并发启动一个任务”。

推论：

- `future` 不构成并发；连续 `co_await` 表示顺序执行。
- 若协程内部不主动让出（例如 `co_await this_task::yield()` 或等待某些会挂起的同步原语），可能导致其它任务长时间得不到运行机会。
- 异常会在 `co_await` 处重新抛出。

### 1.3 运行时约束：必须在 runtime 内等待

`future<T>` 必须在 runtime 上下文中 `co_await`：

- 通过 `core::runtime::block_on(...)` 进入运行时；或
- 在 `spawn(...)` 启动的任务内部等待。

在运行时之外直接 `co_await future` 会触发断言失败或 `panic`。

### 1.4 `[[nodiscard]]`：避免未执行导致的资源泄露

`future<T>` 标记为 `[[nodiscard]]`。若创建 `future` 但从不 `co_await`，该异步调用不会执行完成，通常会导致资源泄露或逻辑缺失。

---

## 2. `join_handle<T>`：运行时任务句柄

当需要并发执行异步逻辑时，使用 `spawn(...)` 将异步函数提交给 runtime，并获得 `join_handle<T>`。

### 2.1 `spawn`：将异步函数提交为任务

`core::runtime::spawn(async_function)` 接收一个返回 `future<T>` 的可调用对象，启动一个并发任务并返回 `join_handle<T>`。

```cpp
#include <asco/core/runtime.h>
#include <asco/future.h>
#include <asco/yield.h>

using namespace asco;

future<int> work() {
    co_await this_task::yield();
    co_return 42;
}

future<void> parent() {
    auto h = spawn([]() -> future<int> { co_return co_await work(); });
    int v = co_await h;  // join
    (void)v;
    co_return;
}
```

### 2.2 `co_await join_handle`：挂起当前协程，等待任务完成

`join_handle<T>` 的等待模型与 `future` 不同：`co_await join_handle<T>` 会挂起当前协程，直到该任务完成。

- 正常完成：返回 `T`（或 `void`）。
- 任务抛异常：在 `co_await` 处重新抛出。

### 2.3 `detach()`：放弃 join

`detach()` 表示调用方不再等待结果，任务将独立执行直至完成/失败。

---

## 3. `join_set<T>`：批量管理并发任务

`join_set<T>` 用于管理一组任务：

- `spawn(...)`：提交任务到 set。
- `co_await set`：按完成顺序收集结果。
- `join_all()`：停止收集并返回已到达的结果。

更完整的语义与注意事项见：[`join_set<T>`：批量任务收集](./join_set.md)。

示例（非 `void` 输出）：

```cpp
#include <asco/core/runtime.h>
#include <asco/join_set.h>
#include <asco/yield.h>

using namespace asco;

future<int> job(int x) {
    co_await this_task::yield();
    co_return x * 2;
}

int main() {
    core::runtime rt;
    return rt.block_on([&]() -> future<int> {
        join_set<int> set{rt};
        for (int i = 0; i < 10; ++i) {
            set.spawn([i]() -> future<int> { co_return co_await job(i); });
        }

        int sum = 0;
        while (auto v = co_await set) {
            sum += *v;
        }

        co_await set.join_all();
        co_return sum;
    });
}
```

---

## 4. `co_invoke`：延长临时可调用对象的生命周期

`co_invoke` 的目的不是简化书写，而是保证“临时可调用对象 + 协程执行”的组合是内存安全的。

### 4.1 风险来源：临时可调用对象生命周期不足

如果把一个**临时可调用对象**（rvalue，例如临时协程 lambda）用于创建 `future/join_handle`，而该任务又会在之后继续执行，那么该临时对象在表达式结束后就会被销毁。

当任务后续执行仍需要访问该可调用对象的捕获状态时，就会触发未定义行为（常见表现为偶发崩溃或数据损坏）。

### 4.2 典型反例：临时协程 lambda

```cpp
future<int> bad() {
    auto f = ([p = std::make_unique<int>(123)]() -> future<int> {
        co_await this_task::yield();
        co_return *p;
    })();

    // 临时 lambda 已销毁；协程恢复后访问 p 可能 UB。
    co_return co_await f;
}
```

该问题通常呈现为偶发崩溃、错误读写或内存破坏，且与根因位置不一致。

### 4.3 `co_invoke` 的机制（对应 `asco/invoke.h`）

`co_invoke` 的语义是：当你用“临时可调用对象”（尤其是临时协程 lambda）来创建 `future/join_handle` 时，框架会确保该临时对象的生命周期足够长，从而避免捕获状态悬垂。

### 4.4 不使用 `co_invoke` 的后果

若在框架/工具函数中对 `Fn&&` 进行天真转发并直接调用：

```cpp
template<class Fn>
auto naive_invoke(Fn&& fn) {
    return std::invoke(std::forward<Fn>(fn));
}
```

则对临时协程可调用对象的调用可能在后续执行中访问已销毁的捕获状态，从而触发未定义行为。该风险通常不会被编译器诊断。

### 4.5 适用范围

- 框架/库作者：只要需要“接受 `Fn&&` 并调用”，且允许传入临时可调用对象，应使用 `co_invoke` 或等价的“将可调用对象绑定到任务以延长生命周期”的方案。
- 一般使用者：通常无需直接调用 `co_invoke`；`spawn(...)`、`join_set::spawn(...)` 等常用入口已经覆盖了该类生命周期风险。

---

## 5. `this_task::yield()`：协作式调度点

`co_await this_task::yield()` 提供一个协作式调度点：让出当前执行权，使其它可运行任务有机会执行。它常用于：

- 避免忙等；
- 提升公平性。

---

## 6. 典型入口：`runtime::block_on(async_main)`

常见工程结构为：

- 用户实现 `future<int> async_main()`。
- `main()` 创建 `core::runtime` 并调用 `block_on(async_main)`。

链接目标 `asco::main` 已提供默认 `main()`；使用者仅需定义 `async_main()`。

---

## 7. 选型摘要

- 在协程中等待一次异步调用：`co_await future<T>`
- 提交并发任务并等待其完成：`spawn(...) -> join_handle<T>`，随后 `co_await`
- 管理多个并发任务：`join_set<T>`
