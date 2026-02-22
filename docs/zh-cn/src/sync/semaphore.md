# `sync::semaphore<N>`：信号量

`sync::semaphore<N>` 表示一个“最多拥有 `N` 个许可（permit）”的信号量。

- 当许可数大于 0 时，`acquire()`/`try_acquire()` 会消耗 1 个许可并成功返回。
- 当许可数为 0 时，`acquire()` 会等待，直到有许可被释放。

常用别名：

- `sync::binary_semaphore`：`sync::semaphore<1>`
- `sync::unlimited_semaphore`：许可上限非常大

头文件：`asco/sync/semaphore.h`

---

## 1. 构造与计数

```cpp
sync::semaphore<3> sem{2};
```

语义：

- 初始许可数为 `min(N, count)`。
- `get_count()` 返回当前许可数（用于观测/调试）。

---

## 2. 获取许可

### 2.1 `try_acquire()`：非等待获取

```cpp
bool ok = sem.try_acquire();
```

语义：

- 若当前许可数大于 0：消耗 1 个许可并返回 `true`。
- 若当前许可数为 0：不等待，直接返回 `false`。

### 2.2 `acquire()`：等待获取

```cpp
co_await sem.acquire();
```

语义：

- 若当前许可数大于 0：消耗 1 个许可并返回。
- 若当前许可数为 0：挂起等待，直到有其它执行流释放许可。

> `acquire()` 返回 `future<void>`，需要在 ASCO runtime 上下文中 `co_await`。

---

## 3. 释放许可

### 3.1 `release(n = 1)`

```cpp
std::size_t released = sem.release(5);
```

语义：

- 向信号量增加最多 `n` 个许可。
- 许可数不会超过上限 `N`。
- 返回值为“本次实际增加的许可数”。

当存在等待 `acquire()` 的执行流时，释放许可会让其中最多 `released` 个等待方继续执行。

---

## 4. 典型用法

### 4.1 `binary_semaphore`：一次只允许一个进入

```cpp
#include <asco/sync/semaphore.h>
#include <asco/future.h>

using namespace asco;

future<void> f() {
    sync::binary_semaphore sem{1};
    co_await sem.acquire();
    // 临界区
    sem.release();
    co_return;
}
```

### 4.2 限流：最多并发 `N` 个任务

```cpp
#include <asco/sync/semaphore.h>
#include <asco/core/runtime.h>
#include <asco/future.h>

using namespace asco;

future<void> limited(sync::semaphore<8> &sem) {
    co_await sem.acquire();
    // ... 执行受限工作
    sem.release();
    co_return;
}
```

---

## 5. 使用建议

- 使用 `try_acquire()` 实现“尽力而为”的快速路径；失败时走其它分支。
- 使用 `acquire()` 表达“必须获得许可才能继续”。
- 不要在未获得许可时调用 `release()` 来“抵消”；这会破坏许可语义。
