# `join_all`：等待多个任务并按参数位置汇总结果

`asco::task::join_all` 用于同时等待多个异步任务，并在全部结束后一次性返回结果。

它适合：

- 你已经知道要等待的任务集合；
- 需要并发运行这些任务；
- 关心“每个参数位置对应哪个结果”，而不是“谁先完成”。

对应头文件：`asco/task/join_all.h`。

---

## 1. 快速上手

```cpp
#include <asco/task/join_all.h>

using namespace asco;

future<int> calc_a() {
    co_return 1;
}

future<int> calc_b() {
    co_return 2;
}

future<int> run() {
    auto [a, b] = co_await task::join_all{
        []() -> future<int> { co_return co_await calc_a(); },
        []() -> future<int> { co_return co_await calc_b(); },
    };

    co_return task::fetch(std::move(a)) + task::fetch(std::move(b));
}
```

要点：

- `join_all` 会并发启动传入的任务。
- 返回值是一个 `std::tuple`，槽位顺序与传入参数顺序一致。
- 每个槽位都是 `std::expected<结果类型, std::exception_ptr>`；成功时取值，失败时保存异常。

---

## 2. API 语义

### 2.1 构造与等待

```cpp
task::join_all(async_fn_1, async_fn_2, ...)
```

语义：

- 每个参数都必须是“异步函数”，即调用后返回 `future<T>`。
- `co_await task::join_all{...}` 会等待所有传入任务结束。
- 返回值是 `std::tuple<std::expected<...>, ...>`。

结果顺序：

- 第 `i` 个 tuple 槽位，总是对应第 `i` 个传入任务。
- 即使任务完成先后顺序不同，返回槽位顺序也不会变化。

异常语义：

- 某个任务抛出异常时，异常会被保存进该槽位的 `std::expected`。
- 其他任务仍会继续运行并各自产生自己的结果槽位。
- 因此，`join_all` 不把“某个子任务失败”作为整体失败直接抛出。

### 2.2 `void` 任务的结果表示

当某个任务返回 `future<void>` 时：

- 对应槽位类型是 `std::expected<std::monostate, std::exception_ptr>`。
- `has_value() == true` 表示该任务成功结束。
- 若任务抛出异常，异常仍保存在 `error()` 中。

### 2.3 `fetch(expected)`：取值或重抛异常

```cpp
template<typename T>
T fetch(std::expected<T, std::exception_ptr> &&e);
```

语义：

- 若 `e.has_value()`，返回其中的值。
- 若 `e` 保存异常，则重抛该异常。

`fetch(...)` 适合在你希望恢复“直接拿值/直接抛错”的调用风格时使用。

---

## 3. 使用约束与建议

### 3.1 适合固定数量的并发等待

`join_all` 更适合“当前这一批任务已知且固定”的场景。

如果你需要：

- 动态追加任务；
- 按完成顺序持续消费结果；
- 在任务尚未全部完成前逐个取回结果；

更适合使用 `join_set<T>`。

### 3.2 不提供单个任务句柄

`join_all` 只返回整体聚合结果，不暴露单个任务的 `join_handle`。

因此：

- 不能通过 `join_all` 单独取消某一个子任务；
- 也不能在等待过程中单独 `co_await` 某一个子任务。

### 3.3 谨慎处理跨挂起点引用生命周期

传入 `join_all` 的异步任务如果捕获了引用，并且这些引用会跨 `co_await` 被使用：

- 调用方必须保证被引用对象在所有相关任务结束前保持有效。

这不是 `join_all` 特有的限制，但在批量并发等待场景中更容易被忽略。
