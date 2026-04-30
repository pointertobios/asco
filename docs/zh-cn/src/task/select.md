# `select`：等待首个完成的异步操作

`asco::task::select` 用于同时等待多个异步操作，并在其中任意一个操作完成后返回该操作的结果。

它适合：

- 需要从多个异步来源中选择最先完成的一项；
- 只关心首个完成结果；
- 首个结果产生后，希望未完成的其他分支收到取消请求。

对应头文件：`asco/task/select.h`。

若需要使用 `task::fetch_result(...)` 辅助取值，还需要包含 `asco/task/fetch_result.h`。

---

## 1. 快速上手

```cpp
#include <variant>

#include <asco/task/fetch_result.h>
#include <asco/task/select.h>

using namespace asco;

future<int> from_cache();
future<int> from_remote();

future<int> run() {
    auto selected = co_await task::select{from_cache, from_remote};

    co_return std::visit(
        [](auto &branch) {
            return task::fetch_result(std::move(branch.value));
        },
        selected);
}
```

要点：

- `select` 会并发启动传入的异步函数（不产生新任务）。
- 返回值是一个 `std::variant`，其中只包含首个完成分支的结果。
- 分支对象提供 `index` 与 `value`：`index` 表示传入参数的位置，`value` 是该分支的 `std::expected<结果类型, std::exception_ptr>`。

---

## 2. API 语义

### 2.1 构造与等待

```cpp
task::select(async_fn_1, async_fn_2, ...)
```

语义：

- 每个参数都必须是“异步函数”，即调用后返回 `future<T>`。
- `co_await task::select{...}` 会等待直到某一个分支率先完成。
- 返回结果只包含首个完成的分支。
- 首个分支完成后，其他尚未完成的分支会收到取消请求。

分支顺序：

- 返回分支的 `index` 对应原始参数位置，从 `0` 开始计数。
- 不要依赖多个分支几乎同时完成时的选择顺序；如果调用方需要稳定汇总全部结果，应使用 `join_all`。

### 2.2 返回值与异常

每个分支的 `value` 类型是 `std::expected<结果类型, std::exception_ptr>`。

语义：

- 若首个完成的分支正常返回，`value.has_value() == true`。
- 若首个完成的分支抛出异常，异常会保存在 `value.error()` 中。
- `select` 不会继续等待其他分支来替代失败结果；抛异常的首个完成分支仍然是被选中的分支。

可以使用 `task::fetch_result(...)` 将 `std::expected` 恢复为“成功返回值，失败重抛异常”的调用风格。

### 2.3 `void` 任务的结果表示

当被选中的分支返回 `future<void>` 时：

- 对应的 `value` 类型是 `std::expected<std::monostate, std::exception_ptr>`。
- `has_value() == true` 表示该分支成功完成。
- 若该分支抛出异常，异常仍保存在 `error()` 中。

---

## 3. 使用约束与建议

### 3.1 只返回首个完成结果

`select` 适合“首个结果足够”的场景。

如果你需要：

- 等待所有分支完成；
- 保留每个参数位置的结果；
- 不希望首个完成后取消其他分支；

更适合使用 `join_all`。

### 3.2 未完成分支需要能正确响应取消

首个分支完成后，`select` 会请求取消未完成分支。

因此，传入的异步操作应确保在收到取消请求后能够正确收束，例如：

- 在需要清理资源时注册取消回调；
- 在长循环中适时检查当前取消状态；
- 避免在不可取消的等待中永久阻塞。

### 3.3 谨慎处理跨挂起点引用生命周期

传入 `select` 的异步任务如果捕获了引用，并且这些引用会跨 `co_await` 被使用：

- 调用方必须保证被引用对象在相关分支完成或取消收束前保持有效。

这不是 `select` 特有的限制，但在“首个完成后取消其他分支”的场景中更容易被忽略。
