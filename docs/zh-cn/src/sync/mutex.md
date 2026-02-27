# `sync::mutex`：互斥锁

`sync::mutex` 用于在多个并发执行流之间建立互斥：同一时刻最多只有一个执行流进入临界区。

它提供两种形态：

- `sync::mutex<>`：只提供互斥，不绑定数据。
- `sync::mutex<T>`：把一个值 `T` 与互斥锁绑定在一起，通过 guard 直接访问该值。

头文件：`asco/sync/mutex.h`

---

## 1. `mutex<>`（无数据）

### 1.1 `lock()`：异步获取

```cpp
#include <asco/sync/mutex.h>
#include <asco/future.h>

using namespace asco;

sync::mutex<> m;

future<void> f() {
    auto g = co_await m.lock();
    // 临界区
    co_return;
} // g 析构后自动解锁
```

语义：

- `lock()` 返回 `future<guard>`；需要在 runtime 上下文中 `co_await`。
- 当锁空闲时：立即获取并返回 guard。
- 当锁被占用时：等待直到锁被释放。
- guard **不可拷贝**、可移动；guard 析构时自动解锁。

### 1.2 `try_lock()`：立即尝试获取

```cpp
if (auto g = m.try_lock()) {
    // 获取成功
} else {
    // 获取失败（不等待）
}
```

语义：

- 若锁空闲：返回有效 guard。
- 若锁被占用：返回空 guard。

### 1.3 `blocking_lock()`：同步阻塞获取

```cpp
auto g = m.blocking_lock();
```

语义：

- 同步阻塞直到获得锁，并返回 guard。
- **禁止**在 ASCO runtime 上下文中调用；若在 runtime 中调用会触发 `panic`。

---

## 2. `mutex<T>`（保护一个值）

`mutex<T>` 将一个值 `T` 与互斥锁绑定。

```cpp
#include <asco/sync/mutex.h>
#include <asco/future.h>

using namespace asco;

sync::mutex<int> counter{0};

future<void> inc() {
    auto g = co_await counter.lock();
    ++(*g);
    co_return;
}
```

语义：

- `lock()` 返回 `future<mutex<T>::guard>`。
- 通过 `*g` / `g->` 访问被保护的 `T`。
- 若 guard 为空（例如 `try_lock()` 失败返回的 guard），对其解引用会触发 `panic`。

---

## 3. guard 的移动语义

- guard 不可拷贝，但可以移动。
- 被移动后的 guard 变为空（`if (g)` 为假）。

---

## 4. 使用建议

- `try_lock()` 适合实现“不等待的快速路径”；失败时走其它分支。
- 互斥锁的临界区尽量保持短小，避免把长时间运行的工作放在持锁期间。
- `blocking_lock()` 仅用于 runtime 之外的同步代码；在 runtime 内请使用 `co_await lock()`。
