# Semaphore（信号量）

`asco::binary_semaphore`、`asco::counting_semaphore<N>` 与 `asco::unlimited_semaphore` 提供了计数型同步原语，可用于限流、互斥（简化版）与事件通知。

- 头文件：`#include <asco/sync/semaphore.h>`
- 命名空间：`asco`（类型别名定义在全局 `asco` 命名空间下）

## 类型与别名

- `binary_semaphore`：上限为 1 的二值信号量。
- `template<size_t N> counting_semaphore`：上限为 `N` 的计数信号量。
- `unlimited_semaphore`：上限为 `size_t` 的最大值，可视为“无上限”。

底层实现均基于 `semaphore_base<CountMax>`。

## 接口

- `bool try_acquire() noexcept`
  - 若计数大于 0，则原子地减 1 并返回 `true`；否则返回 `false`。
- `future<void> acquire()`
  - 若计数为 0，则当前协程挂起，直到被 `release()` 唤醒；恢复后原子地减 1。
- `future<bool> acquire_for(const duration_type auto &timeout)`
  - 尝试在指定超时时间内获取许可，超时则返回 `false`。
- `future<bool> acquire_until(const time_point_type auto &expire_time)`
  - 尝试在指定时间点前获取许可，超时则返回 `false`。
- `void release(size_t update = 1)`
  - 将计数增加 `min(update, CountMax - old_count)`，并唤醒相应数量的等待者。

特性：

- `acquire()` 以协程方式挂起，不阻塞线程。
- `release()` 支持一次性增加多个许可；对上限做饱和值裁剪。
- 公平性：不保证严格公平，但能够唤醒同等数量等待者。

## 使用示例

### 1. 事件通知（先通知，后等待）

```cpp
#include <asco/future.h>
#include <asco/sync/semaphore.h>
using namespace asco;

future<int> async_main() {
    binary_semaphore sem{0};

    // 先通知
    sem.release();

    // 后等待：不会永久挂起
    co_await sem.acquire();
    co_return 0;
}
```

### 2. 等待后再通知（跨任务）

```cpp
#include <asco/future.h>
#include <asco/sync/semaphore.h>
#include <atomic>
using namespace asco;

future_spawn<void> worker(binary_semaphore &sem, std::atomic<bool> &done) {
    co_await sem.acquire();
    done.store(true, std::memory_order::release);
    co_return;
}

future<int> async_main() {
    binary_semaphore sem{0};
    std::atomic<bool> done{false};

    auto w = worker(sem, done);

    // 使等待中的任务恢复
    sem.release();
    co_await w;

    co_return done.load(std::memory_order::acquire) ? 0 : 1;
}
```

### 3. 高并发场景（多次 release / acquire）

```cpp
#include <asco/future.h>
#include <asco/sync/semaphore.h>
#include <atomic>
using namespace asco;

future_spawn<void> consumer(counting_semaphore<1000> &sem, std::atomic<size_t> &cnt) {
    for (size_t i = 0; i < 1000; ++i) {
        co_await sem.acquire();
        cnt.fetch_add(1, std::memory_order::relaxed);
    }
    co_return;
}

future<int> async_main() {
    counting_semaphore<1000> sem{0};
    std::atomic<size_t> cnt{0};

    auto c = consumer(sem, cnt);

    for (size_t i = 0; i < 1000; ++i) sem.release();

    co_await c;
    co_return cnt.load(std::memory_order::relaxed) == 1000 ? 0 : 1;
}
```

## 注意事项

- `release()` 的 `update` 值会根据上限裁剪，避免溢出。
