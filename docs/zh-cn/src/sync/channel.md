# Channel（通道）

ASCO 的 Channel 为多生产者/多消费者（MPMC）数据通道：

- 结合 `unlimited_semaphore` 完成“有数据即通知”的等待/唤醒。

- 头文件：`#include <asco/sync/channel.h>`
- 命名空间：
  - 类型：`asco::sender<T>`, `asco::receiver<T>`
  - 工厂：`asco::channel<T>()`

## 构造与类型

- `auto [tx, rx] = channel<T>();`
  - 返回 `sender<T>` 与 `receiver<T>` 的二元组，二者共享同一个内部信号量。
- 也可传入自定义队列工厂：`channel<T>(Creator)`，其中 `Creator` 需满足 `queue::creator` 概念。

## 接口语义

### sender

- `future<std::optional<std::tuple<T, queue::push_fail>>> send(T value)`
  - 调用方需 `co_await` 该 `future`，以便在底层使用有界队列满载时自动等待配额。
  - 成功：返回 `std::nullopt`，并唤醒一个等待的接收者。
  - 失败：返回包含原值与失败原因的 `std::tuple<T, queue::push_fail>`：
    - `push_fail::closed`：队列已关闭或停止。
    - `push_fail::full`：自定义队列已满且不可再写入。
- `void stop() noexcept`
  - 标记底层队列为“发送端停止”。调用后不得再调用 `send()`；否则底层会触发 panic（调试保护）。
- `bool is_stopped() const noexcept`
  - 若发送端或接收端已停止，或未绑定到队列，返回 `true`。

### receiver

- `std::expected<T, pop_fail> try_recv()`
  - 若无可用数据：返回 `std::unexpected(pop_fail::non_object)`。
  - 若通道已完全关闭且无可读对象：可能返回 `std::unexpected(pop_fail::closed)`。
  - 若成功：返回 `T`。
- `future<std::optional<T>> recv()`
  - 若当前无数据：协程方式挂起直至有“数据就绪”的通知；
  - 唤醒后尝试读取，若成功返回 `T`，若关闭导致无对象可读返回 `std::nullopt`。当底层队列为有界队列时，成功读取还会释放一个配额令发送端继续写入。
- `bool is_stopped() const noexcept`
  - 当发送端或接收端停止，且当前帧已读尽时，返回 `true`。

## 有序性与并发性

- Channel 保持 FIFO 顺序。
- 支持 MPMC：多个 sender/receiver 并发安全。

## 典型用法

### 1. 单生产者-单消费者

```cpp
#include <asco/future.h>
#include <asco/sync/channel.h>
#include <print>
using namespace asco;

future<int> async_main() {
    auto [tx, rx] = channel<int>();

    // 发送
    for (int i = 0; i < 5; ++i) {
      if (auto r = co_await tx.send(i); r.has_value()) {
        auto &[value, reason] = *r;
        std::println("send failed: {} (reason = {})", value, static_cast<int>(reason));
            co_return 1;
        }
    }

    // 接收（等待式）
    for (int i = 0; i < 5; ++i) {
        auto v = co_await rx.recv();
        if (!v || *v != i) {
            std::println("recv mismatch: {}", v ? *v : -1);
            co_return 1;
        }
    }

    co_return 0;
}
```

### 2. 停止与排干

```cpp
#include <asco/future.h>
#include <asco/sync/channel.h>
using namespace asco;

future<int> async_main() {
    auto [tx, rx] = channel<int>();

    (void)co_await tx.send(7);
    (void)co_await tx.send(8);

    // 停止发送端：之后不得再调用 send()
    tx.stop();

    // 排干剩余元素
    auto a = co_await rx.recv();
    auto b = co_await rx.recv();

    if (!a || !b || *a != 7 || *b != 8) co_return 1;
    if (!tx.is_stopped() || !rx.is_stopped()) co_return 1;

    // 现在应无更多数据
    auto t = rx.try_recv();
    if (t.has_value()) co_return 1;

    co_return 0;
}
```

### 3. 非阻塞读取与等待混合

```cpp
#include <asco/future.h>
#include <asco/sync/channel.h>
#include <asco/time/interval.h>
#include <print>
using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    auto [tx, rx] = channel<int>();

    // 生产者：异步发送
    auto producer = [] (sender<int> tx) -> future_spawn<void> {
        for (int i = 0; i < 3; ++i) (void)co_await tx.send(i);
        tx.stop();
        co_return;
    }(tx);

    interval tick{100ms};
    while (!rx.is_stopped()) {
        // 先尝试非阻塞
        if (auto r = rx.try_recv(); r.has_value()) {
            std::println("got {}", *r);
            continue;
        }
        // 无数据则小憩一会儿（避免空转）
        co_await tick.tick();
    }

    co_await producer;
    co_return 0;
}
```

## 注意事项

- 始终 `co_await sender::send()` 以正确处理带界队列的背压；忽略返回 `future` 可能导致任务提前退出或绕过容量控制。
- `sender::send()` 返回值含 `queue::push_fail`，常见原因是通道已关闭（`push_fail::closed`）；若自定义队列有容量限制，可额外关注 `push_fail::full`。
- 调用 `sender::stop()` 后不可再调用 `send()`；这是通道关闭的显式信号，违背会触发 panic（调试保护）。
- `recv()` 被唤醒后理论上应能读到元素；若底层已关闭并无对象可读将得到 `std::nullopt`。
- 使用 `try_recv()` 判空时，请正确处理 `pop_fail::non_object` 与 `pop_fail::closed`。
