# Context（协程上下文）

`asco::context`（底层定义在 `asco::contexts::context`）提供了一个轻量的协程取消原语，可用于在多个协程之间传播“停止/超时”信号。它与 `notify` 结合使用，支持显式取消与超时自动取消。

- 头文件：`#include <asco/context.h>`
- 命名空间：`asco`（通过别名导出）

## 创建方式

- `static std::shared_ptr<context> with_cancel()`
  - 创建一个手动可取消的上下文，初始状态为 *未取消*。
- `static std::shared_ptr<context> with_timeout(const duration_type auto &dur)`
  - 创建一个上下文并在 `dur` 后自动取消。内部会启动一个协程调用 `cancel()`。

两个工厂函数都返回 `std::shared_ptr<context>`。使用共享指针便于在多个协程中传播同一取消源。

## 取消与状态查询

- `void cancel() noexcept`
  - 将上下文标记为已取消，并唤醒所有等待该上下文的协程。
- `bool is_cancelled() const noexcept`
  - 查询当前是否处于已取消状态。

取消操作是幂等的；重复调用 `cancel()` 不会带来额外副作用。

## 等待取消

`context` 定义了成员 `operator co_await()`，同时也为 `std::shared_ptr<context>` 提供了自由函数 `operator co_await()`。因此既可以在上下文对象本身上 `co_await ctx_ref;`，也可以直接 `co_await ctx_ptr;` 来等待取消事件：

- 若上下文尚未取消，当前协程将挂起，直到 `cancel()` 或超时触发。
- 若上下文已取消，则立即恢复，且 `co_await` 不返回任何额外数据：它仅表示“取消已经发生”。

示例：

```cpp
#include <asco/context.h>
#include <asco/future.h>
#include <asco/time/sleep.h>
using namespace asco;

future_spawn<void> worker(context &ctx_ref, std::atomic<bool> &flag) {
  co_await ctx_ref;            // 等待取消信号
    flag.store(true, std::memory_order_release);
    co_return;
}

future<int> async_main() {
    auto ctx = context::with_cancel();
    std::atomic<bool> flag{false};

  auto w = worker(*ctx, flag);  // 也可以 co_await ctx（shared_ptr）

    // 进行其他操作…
    co_await sleep_for(10ms);
    ctx->cancel();               // 通知所有等待方

    co_await w;
    return flag.load(std::memory_order_acquire) ? 0 : 1;
}
```

## 与超时结合

`with_timeout()` 会在后台调用 `sleep_for()` 后自动取消：

```cpp
future<int> async_main() {
    auto ctx = context::with_timeout(50ms);

    co_await ctx;  // 最多等待 50ms

    // 此时 ctx 已经处于取消状态
    if (!ctx->is_cancelled()) {
        co_return 1;
    }
    co_return 0;
}
```

## 注意事项

- `context` 仅负责取消信号的传播，不携带附加信息。若需要携带错误码或取消原因，请在业务代码中自行维护。
- `co_await ctx;` 只表示“取消事件发生”，不要尝试从中读取返回值。
- 上下文内部使用 `notify` 唤醒等待者；在没有协程等待时调用 `cancel()` 也会正确记录状态，随后等待者会立即返回。
