# 任务取消机制

ASCO 的“任务取消”主要面向由 runtime 调度的任务（`join_handle<T>`）。它的目标是：

- 由外部请求取消一个正在运行/挂起的任务；
- 任务内可以通过 token 观察取消请求，并注册回调执行清理/通知；
- 取消发生并被处理后，该任务会被终止，不再继续执行。

> 术语：本文的“任务”指 `spawn(...)` 产生的 runtime 任务（`join_handle`）。

---

## 1. 取消的组成

相关 API 位于：

- `asco/core/cancellation.h`
- `asco/this_task.h`
- `asco/join_handle.h`

核心类型：

- `asco::core::cancel_source`：取消信号的来源
  - `request_cancel()`：请求取消（仅发出 stop 请求）
  - `invoke_callbacks()`：执行已注册的取消回调（LIFO 顺序）
- `asco::core::cancel_token`：取消信号的观察者
  - `cancel_requested()`：查询是否已请求取消
  - `close_cancellation()` / `cancellation_closed()`：关闭/查询“取消关闭”状态
- `asco::core::cancel_callback`
  - 基于 token 注册一个回调；对象析构时自动注销

任务内入口（当前正在运行的任务）：

- `asco::this_task::get_cancel_token()`：获取当前任务的 `cancel_token`
- `asco::this_task::close_cancellation()`：关闭当前任务的取消（见第 4 节）

---

## 2. 如何取消一个任务：`co_await join_handle::cancel()`

外部取消的标准方式是：对 `join_handle<T>` 调用并等待 `cancel()`：

```cpp
#include <asco/future.h>
#include <asco/yield.h>

using namespace asco;

future<void> example_cancel(join_handle<void> &h) {
    co_await h.cancel();

    bool cancelled = false;
    try {
        co_await h;  // join
    } catch (core::coroutine_cancelled &) {
        cancelled = true;
    }

    // cancelled == true
    (void)cancelled;
}
```

行为要点：

- `cancel()` 会给该任务的 `cancel_source` 发出 stop 请求。
- 取消请求被处理后，会执行已注册的取消回调（`cancel_callback`），并终止该任务的后续执行。
- 被取消的 `join_handle` 在 `co_await` 时会抛出 `asco::core::coroutine_cancelled`。

---

## 3. 任务内如何响应取消

### 3.1 首选：注册取消回调（`cancel_callback`）

如果你的代码需要在取消发生时执行某些动作（比如设置 flag、通知别的协程/线程），可以注册回调：

```cpp
#include <atomic>
#include <asco/cancellation.h>
#include <asco/this_task.h>

using namespace asco;

future<void> with_cancel_callback(std::atomic_bool &flag) {
    auto &token = this_task::get_cancel_token();
    core::cancel_callback cb{token, [&]() { flag.store(true, std::memory_order::release); }};

    while (!flag.load(std::memory_order::acquire)) {
        co_await this_task::yield();
    }

    co_return;
}
```

语义：

- 回调在取消请求被处理时执行。
- 回调的常见用途是：触发一次“通知/标记/释放资源/恢复状态”的动作。

回调顺序与生命周期：

- 取消回调以 **LIFO** 顺序执行。

### 3.2 补充：查询取消请求（`cancel_requested()`）

如果你希望在协程的“安全点”主动结束逻辑，可以轮询当前任务的取消状态：

```cpp
#include <asco/this_task.h>

using namespace asco;

future<void> worker_loop() {
    while (true) {
        if (this_task::get_cancel_token().cancel_requested()) {
            co_return;
        }
        co_await this_task::yield();
    }
}
```

语义：

- 当 `cancel_requested()` 为 `true` 时，表示任务已被请求取消。
- 该写法适合：你需要在循环/阶段边界按自己的方式收尾并退出。

---

## 4. 关闭取消：`this_task::close_cancellation()`

某些关键区段你可能希望“禁止外部取消”，以避免破坏内部一致性。ASCO 提供：

```cpp
#include <asco/this_task.h>

using namespace asco;

future<void> critical_section() {
    this_task::close_cancellation();

    // ... 做一些你不希望被外部取消打断的工作

    co_return;
}
```

行为语义：

- 调用 `this_task::close_cancellation()` 后，该任务会进入“取消已关闭”状态。
- 若之后仍对该任务发出取消请求，会触发 `panic`。
- 取消关闭是单向的：关闭后无法再打开。

因此建议：

- 只在你能证明“此后不会再被外部取消”的场景使用它；
- 更常见的做法是：不关闭取消，而是让任务在安全点自行检查 token 并退出。

---

## 5. 常见坑与建议

- `this_task::get_cancel_token()` / `close_cancellation()` **只能在 runtime 中调用**；不在 runtime 会 `panic`。
- 取消回调适合做“通知/打点/设置标志/清理资源/恢复状态”，不要在回调里做复杂阻塞操作。
- 如果你需要让自定义 awaiter 支持取消：在 `await_suspend` 时用 `cancel_callback` 监听 token，并在回调里安排唤醒/中断（可参考仓库内 `tests/cancellation.cpp` 的 awaiter 写法）。
