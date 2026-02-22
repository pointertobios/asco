# `join_set<T>`：按完成顺序收集并发任务

`join_set<T>` 用于**批量启动任务**，并按“任务完成顺序”持续产出结果。

它适合：

- 你要同时跑很多个任务；
- 想边完成边消费结果（streaming），而不是固定按提交顺序等待。

对应头文件：`asco/join_set.h`。

---

## 1. 快速上手

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
            // v 的到达顺序 = 完成顺序（不保证与 i 相同）
            sum += *v;
        }

        co_return sum;
    });
}
```

要点：

- `set.spawn(...)` 提交任务；`join_set` 不提供单个任务的 `join_handle`。
- `co_await set` 每次取回一个结果；当 set 中已提交任务全部产出结果后，返回 `std::nullopt`。

---

## 2. API 语义

### 2.1 构造

- `join_set()`：使用 `core::runtime::current()`
- `join_set(core::runtime&)`：显式绑定 runtime

### 2.2 `spawn(async_fn)`：提交异步任务

```cpp
void spawn(async_function<> auto &&fn);
```

语义：

- `fn()` 必须返回 `future<T>`（即“异步函数”）。
- `spawn(...)` 会启动一个并发任务。
- 当该任务完成并产生结果后，结果会出现在 `co_await set` 的返回序列中（按完成顺序）。

注意：

- `join_set` 不暴露单个任务的 `join_handle`，因此无法对单个任务进行 `cancel()` 或单独 `co_await`。
- 传入的任务应当在完成路径上**总能产出一个结果**；如果任务提前退出且没有产出结果，`join_set` 将永远等不到该条结果。

### 2.3 `co_await set`：取回一个结果

```cpp
future<std::optional<T>> operator co_await();
```

当 `T` 为非 `void` 类型时：

- 每次 `co_await set` 返回一个 `std::optional<T>`
  - `has_value() == true`：拿到一个结果
  - `std::nullopt`：当前 `join_set` 中已提交任务的结果都已收齐

当 `T` 为 `void` 时：

- 每次 `co_await set` 返回一个 `bool`
  - `true`：拿到一个结果
  - `false`：当前 `join_set` 中已提交任务的结果都已收齐

### 2.4 `join_all()`：停止产生新任务并收集已产生的结果

`join_all()` 会停止新的 `spawn`，并返回一个 `std::vector<T>`（包含已收集到的结果）。

---

## 3. 常见问题与建议

### 3.1 任务异常如何处理？

`join_set` 不会把“任务失败”作为结果返回给你。为了保证 `join_set` 能持续产出结果：

- 不要让异常从任务中逃逸；
- 推荐把错误编码进返回值（例如 `std::expected<T, E>`），并始终返回一个值。

工程建议：

- 让任务返回 `std::expected<T, E>` 或类似结果类型，把错误显式作为值传回；
- 或在任务内部 `try/catch`，把错误信息转成 `T` 的某种错误表示。

### 3.2 `spawn_blocking` 并不把工作移到“后台线程”

`spawn_blocking(...)` 不会把工作自动转移到其它线程。

因此：

- 避免在其中执行长时间阻塞操作（例如长 IO、长时间 sleep）。

### 3.3 什么时候该用 `join_handle` 而不是 `join_set`？

- 需要对某个任务单独 `cancel()` / `detach()` / `co_await`：用 `spawn(...) -> join_handle<T>`。
- 需要批量启动、按完成顺序消费结果：用 `join_set<T>`。
