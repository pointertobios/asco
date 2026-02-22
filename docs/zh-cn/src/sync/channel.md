# `sync::channel<T>`：通道

`sync::channel<T>` 用于在并发执行流之间传递值。

- 它是**有界**的：当缓冲区满时，发送会等待。
- 当缓冲区空时，接收会等待。
- 通道被关闭后，发送会失败；接收在“通道已关闭且缓冲已空”时结束。

头文件：`asco/sync/channel.h`

---

## 1. 创建通道

```cpp
#include <asco/sync/channel.h>

auto [tx, rx] = asco::sync::channel<int>();
```

语义：

- `channel<T>()` 返回一对端点：`sender<T>`（发送端）与 `receiver<T>`（接收端）。
- `sender<T>`/`receiver<T>` **可拷贝**；拷贝后的对象与原对象共享同一个通道。

---

## 2. 发送：`sender<T>::send(...)`

### 2.1 `T` 非 `void`

```cpp
#include <asco/sync/channel.h>
#include <asco/future.h>
#include <expected>

using namespace asco;

future<void> producer(sync::sender<int> tx) {
    auto r = co_await tx.send(42);
    if (!r) {
        // 发送失败：通道已关闭，42 没有被发送
        int unsent = std::move(r.error());
        (void)unsent;
    }
    co_return;
}
```

语义：

- `send(value)` 返回 `future<std::expected<std::monostate, T>>`。
- 当通道可写时：把 `value` 发送到通道中，并返回“成功”。
- 当缓冲区满时：等待直到通道可写或通道关闭。
- 当通道已关闭且本次发送未发生时：返回“失败”，并在 `error()` 中返回未发送的 `value`。

### 2.2 `T == void`

当 `T` 为 `void` 时，发送表示发送一个“事件”。

```cpp
#include <asco/sync/channel.h>
#include <asco/future.h>

using namespace asco;

future<void> producer(sync::sender<void> tx) {
    bool ok = co_await tx.send();
    if (!ok) {
        // 通道已关闭，本次事件没有被发送
    }
    co_return;
}
```

语义：

- `send()` 返回 `future<bool>`。
- 返回 `true` 表示发送成功；返回 `false` 表示通道已关闭且本次发送未发生。

---

## 3. 接收：`receiver<T>::recv()`

```cpp
#include <asco/sync/channel.h>
#include <asco/future.h>
#include <optional>

using namespace asco;

future<void> consumer(sync::receiver<int> rx) {
    while (auto v = co_await rx.recv()) {
        int x = *v;
        (void)x;
    }
    // 循环结束：通道已关闭且缓冲已空
    co_return;
}
```

语义（`T` 非 `void`）：

- `recv()` 返回 `future<std::optional<T>>`。
- 当通道中存在值时：返回 `T`。
- 当通道暂时为空且未关闭时：等待直到有值可读或通道关闭。
- 当通道已关闭且缓冲已空时：返回 `std::nullopt`。

---

## 4. 关闭通道：`stop()`

发送端与接收端都提供 `stop()`：

```cpp
tx.stop();
rx.stop();
```

语义：

- 调用 `stop()` 会关闭通道。
- 关闭后：新的发送不会发生；接收在读完缓冲中的剩余值后结束（`recv()` 返回 `std::nullopt`）。

---

## 5. 使用建议

- 当需要把数据从生产者传给消费者，并让双方通过等待来表达背压时，使用 `channel<T>`。
- 若你只需要“完成通知/事件”，可以用 `channel<void>` 表达事件流。
- 当某一侧确定不再使用通道时，调用 `stop()` 让另一侧尽快结束等待并退出。
