# `sync::rwlock`：读写锁

`sync::rwlock` 用于保护“读多写少”的共享状态。

- 多个 reader 可以同时持有读锁；
- writer 必须独占；
- 一旦有 writer 开始等待，后续 reader 不会再继续插队。
- reader 可以尝试把自己持有的读锁原子地升级为写锁。

它提供两种形态：

- `sync::rwlock<>`：只提供读写锁语义，不绑定数据。
- `sync::rwlock<T>`：把一个值 `T` 与读写锁绑定，通过 guard 直接访问该值。

头文件：`asco/sync/rwlock.h`

---

## 1. `rwlock<>`（无数据）

### 1.1 `write()` / `try_write()`：获取写锁

```cpp
#include <asco/sync/rwlock.h>
#include <asco/future.h>

using namespace asco;

sync::rwlock<> lock;

future<void> mutate() {
    auto g = co_await lock.write();
    // 写临界区
    co_return;
}

void maybe_mutate() {
    if (auto g = lock.try_write()) {
        // 立即拿到写锁
    }
}
```

语义：

- `write()` 返回 `future<write_guard>`，需要在 runtime 上下文中 `co_await`。
- 只有在“没有 reader、也没有 writer”的情况下，writer 才能进入。
- 若存在活跃 reader 或 writer：`write()` 等待，`try_write()` 直接失败。
- 一旦某个 writer 开始等待，后续 reader 不再继续进入。这让 writer 在已有 reader 退出后优先获得锁，避免被新 reader 长期饿死。
- `write_guard` 不可拷贝、可移动；析构时自动释放写锁。

### 1.2 `read()` / `try_read()`：获取读锁

```cpp
#include <asco/sync/rwlock.h>
#include <asco/future.h>

using namespace asco;

sync::rwlock<> lock;

future<void> inspect() {
    auto g = co_await lock.read();
    // 读临界区
    co_return;
}

void fast_path() {
    if (auto g = lock.try_read()) {
        // 立即拿到读锁
    }
}
```

语义：

- `read()` 返回 `future<read_guard>`，需要在 runtime 上下文中 `co_await`。
- 若当前没有 writer 持有或等待：reader 立即进入。
- 多个 reader 可以同时成功获取。
- 若当前已有 writer 持有，或已经有 writer 在等待：新的 reader 会等待；`try_read()` 则直接失败。
- `read_guard` 不可拷贝、可移动；析构时自动释放读锁。

### 1.3 `read_guard::upgrade()`：读锁升级为写锁

```cpp
#include <asco/future.h>
#include <asco/sync/rwlock.h>
#include <utility>

using namespace asco;

future<void> maybe_promote(sync::rwlock<> &lock) {
    auto g = co_await lock.read();

    // ... 先读取共享状态

    auto wg = co_await std::move(g).upgrade();
    if (!wg) {
        // 升级失败：当前读视图不再有效，需要自行决定是否重试
        co_return;
    }

    // 现在已经持有写锁
    co_return;
}
```

语义：

- `upgrade()` 定义在 `read_guard` 上，返回 `future<write_guard>`。
- 它会消耗当前 `read_guard`，因此调用形式应为 `std::move(g).upgrade()`。
- 若当前 reader 已经是最后一个 reader：会直接升级为 writer。
- 若当前没有别的升级者，但仍有其它 reader：会等待直到其它 reader 释放，然后把当前 guard 升级为写锁。
- 若此时已经有另一个 `upgrade()` 在进行中：当前 `upgrade()` 会失败，并返回空的 `write_guard`。
- 升级开始后，会像等待中的 writer 一样阻止新的 reader 继续进入。
- 若 guard 为空，`upgrade()` 返回空的 `write_guard`。

这意味着 `upgrade()` 的成功语义是“当前读视图被原子地提升为写锁”；一旦做不到这个保证，API 会失败，而不是等待到某个未来时刻再给你一个写锁。

---

## 2. `rwlock<T>`（保护一个值）

`rwlock<T>` 把一个值 `T` 与读写锁绑定：

- 写锁返回可写 guard。
- 读锁返回只读 guard；

```cpp
#include <asco/sync/rwlock.h>
#include <asco/future.h>

using namespace asco;

struct config {
    int version;
    bool enabled;
};

sync::rwlock<config> cfg{config{1, true}};

future<void> read_cfg() {
    auto g = co_await cfg.read();
    int version = g->version;
    bool enabled = g->enabled;
    (void)version;
    (void)enabled;
    co_return;
}

future<void> update_cfg() {
    auto g = co_await cfg.write();
    g->version += 1;
    g->enabled = false;
    co_return;
}
```

语义：

- `co_await lock.write()` 返回 `rwlock<T>::write_guard`，通过 `*g` / `g->` 读写访问 `T`。
- `co_await lock.read()` 返回 `rwlock<T>::read_guard`，通过 `*g` / `g->` 只读访问 `T`。
- `co_await std::move(read_guard).upgrade()` 返回 `rwlock<T>::write_guard`，用于尝试把当前读锁原子升级成写锁；失败时返回空 guard。
- `try_read()` / `try_write()` 分别返回对应 guard；失败时返回空 guard。
- 若 guard 为空（例如 `try_*()` 失败），对其解引用会触发 `panic`。

---

## 3. 调度与公平性

`rwlock` 的行为重点不是“严格公平”，而是“writer 不被新 reader 持续插队”：

- 已经持有读锁的 reader 可以并发完成；
- 等待中的 writer 会阻止后续 reader 再进入；
- 被唤醒的具体等待方顺序不保证是 FIFO。

如果你的场景是：

- 读操作远多于写操作；
- 读操作之间可以安全并发；
- 写操作希望在已有 reader 完成后尽快获得独占权限；

那么 `rwlock` 比 `mutex` 更合适。

---

## 4. 使用建议

- 用 `try_read()` / `try_write()` 表达“不等待的快速路径”；失败时自行走降级逻辑。
- 如果你一开始就知道自己要修改共享状态，优先直接使用 `write()` / `try_write()`；`upgrade()` 适合先读后判定是否需要写入的路径。
- 需要从读路径进入写路径时，用 `std::move(g).upgrade()`；不要先手动释放读锁再去重新 `write()`，否则中间会出现竞争窗口。
- `upgrade()` 失败时，原 `read_guard` 已经被消费；此时应丢弃先前读到的状态，并从头重新执行“读取-校验-升级/写入”流程。
- 不要假设支持“写锁降级为读锁”；当前 API 没有提供这种能力。
- 同一把 `rwlock` 上同时只能有一个升级者继续等待；后续并发 `upgrade()` 会快速失败并返回空 guard。
- 如果读写比例并不明显偏向“读多写少”，或者临界区本身非常短，优先考虑语义更直接的 `mutex`。
