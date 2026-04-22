# 任务取消机制

ASCO 的“任务取消”主要面向由 runtime 调度的任务（`join_handle<T>`）。它的目标是：

- 由外部请求取消一个正在运行/挂起的任务；
- 任务内可以观察当前取消状态，并在取消发生时执行清理或通知；
- 取消发生并被处理后，该任务会被终止，不再继续执行。

> 术语：本文的“任务”指 `spawn(...)` 产生的 runtime 任务（`join_handle`）。
>
> 当前公开语义同时包含两层行为：
>
> - 每个任务或嵌套异步操作都有自己的取消状态与取消回调；
> - 当外层任务等待内部异步操作时，外层取消会一并终止那些仍未完成的内部操作。

---

## 1. 取消的组成

相关 API 位于：

- `asco/core/cancellation.h`
- `asco/this_task.h`
- `asco/join_handle.h`

核心类型：

- `asco::core::cancel_source`：取消信号的来源
  - `request_cancel()`：发出取消请求，仅设置 stop 请求本身
    - `invoke_callbacks()`：执行当前上下文已注册的取消回调；回调按后注册先执行的顺序调用
- `asco::core::cancel_token`：取消信号的观察者
  - `cancel_requested()`：查询当前上下文是否已请求取消
- `asco::core::cancel_callback`
  - 为当前上下文注册一个回调，用于在取消发生时执行清理或通知
  - 在这种用法下，对象析构时自动注销

任务内入口（当前正在运行的任务）：

- `asco::this_task::get_current_cancel_token()`：获取当前上下文的 `cancel_token`

---

## 2. 如何取消一个任务：`join_handle::cancel()`

外部取消的标准方式是：先对 `join_handle<T>` 调用 `cancel()`，再在需要时通过 `co_await` 该句柄观察取消结果：

```cpp
#include <asco/future.h>
#include <asco/yield.h>

using namespace asco;

future<void> example_cancel(join_handle<void> &h) {
    h.cancel();

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

- 取消被任务观察并收束后，会执行已注册的取消回调（`cancel_callback`），并结束该任务后续执行。
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
    core::cancel_callback cb{[&]() { flag.store(true, std::memory_order::release); }};

    while (!flag.load(std::memory_order::acquire)) {
        co_await this_task::yield();
    }

    co_return;
}
```

语义：

- 回调在取消请求被处理时执行。
- 回调的常见用途是：触发一次“通知/标记/释放资源/恢复状态”的动作。
- 回调总是绑定到“当前正在执行的这段异步操作”；如果代码运行在 `task::join_all(...)` 等嵌套等待内部操作的场景中，回调只影响它所在的那一项操作。

回调顺序与生命周期：

- `cancel_callback` 的预期用法，是在当前任务内作为局部对象注册一个取消回调。
- 注册与销毁应发生在同一任务上下文中；它不适合作为跨任务、跨所有权边界传递的通用对象使用。
- 在这种局部 RAII 用法下，对象析构时会自动注销对应回调。
- 取消回调以“后注册先执行”的顺序调用；这里说的是同一任务中按嵌套作用域注册的多个回调。
- 不要把 `cancel_callback` 当成可以长期保存、放入共享状态、容器或堆对象中并任意延后销毁的通用注册对象使用。

### 3.2 补充：查询取消请求（`cancel_requested()`）

如果你希望在协程的安全点主动结束逻辑，可以轮询当前取消状态：

```cpp
#include <asco/this_task.h>

using namespace asco;

future<void> worker_loop() {
    auto &token = this_task::get_current_cancel_token();

    while (true) {
        if (token.cancel_requested()) {
            co_return;
        }
        co_await this_task::yield();
    }
}
```

语义：

- 当 `cancel_requested()` 为 `true` 时，表示当前这段异步操作已被请求取消。
- 该写法适合：你需要在循环/阶段边界按自己的方式收尾并退出。

---

## 4. 层级化取消

某些组合等待场景会在当前任务内部同时推进多个异步操作，例如 `task::join_all(...)`。

行为语义：

- 如果外层任务在等待这些内部操作时被取消，仍未完成的内部操作也会结束。
- 内部操作中注册的 `cancel_callback` 仍会执行，因此对应的清理逻辑可以放在回调里。
- 对于需要可靠清理的代码，优先使用 `cancel_callback`，不要只依赖循环里偶尔轮询一次 `cancel_requested()`。

使用建议：

- 当子任务需要释放资源、唤醒等待者或设置完成标志时，优先注册 `cancel_callback`。
- 当你只需要在本段异步逻辑的阶段边界主动退出时，再轮询 `get_current_cancel_token().cancel_requested()`。

---

## 5. 常见坑与建议

- `this_task::get_current_cancel_token()` **只能在 runtime 中调用**；不在 runtime 会 `panic`。
- 取消回调适合做“通知/打点/设置标志/清理资源/恢复状态”，不要在回调里做复杂阻塞操作。
- `cancel_callback` 绑定的是当前这段异步操作；不要把它当成跨任务、跨上下文复用的通用注册句柄。
- 如果你需要让自定义 awaiter 支持取消，可以在挂起前注册 `cancel_callback`，并在回调里安排唤醒或中断。
- 任何一个挂起点都有可能执行取消，如果一个异步函数需要支持取消，应确保所有挂起点都能正确响应取消请求，如注册 `cancel_callback` 或查询 `cancel_requested()`。
