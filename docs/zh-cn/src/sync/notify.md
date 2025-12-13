# Notify（轻量通知器）

`asco::notify` 提供一个极轻量的事件通知原语，支持多个协程等待并由通知唤醒。它基于内部的 `wait_queue`，适合在不需要复杂状态同步、只需“唤醒即可”的场景中使用。

- 头文件：`#include <asco/sync/notify.h>`
- 命名空间：`asco`

## 接口概览

- `yield<> wait()`
  - 以协程方式挂起当前任务，等待 `notify_one()` 或 `notify_all()` 唤醒。
- `void notify_one()`
  - 唤醒至多一个等待中的协程。若当前没有等待者，通知会被丢弃（不计数、不排队）。
- `void notify_all()`
  - 唤醒所有等待中的协程，同样不会记录通知历史。

## 使用示例

### 1. 简单的等待-通知

```cpp
#include <asco/future.h>
#include <asco/sync/notify.h>
using namespace asco;

future_spawn<void> worker(notify &n, std::atomic<bool> &flag) {
    co_await n.wait();
    flag.store(true, std::memory_order_release);
    co_return;
}

future<int> async_main() {
    notify n;
    std::atomic<bool> flag{false};

    auto w = worker(n, flag);

    // 执行若干工作…
    n.notify_one();

    co_await w;
    co_return flag.load(std::memory_order_acquire) ? 0 : 1;
}
```

### 2. 广播唤醒多个等待者

```cpp
#include <asco/future.h>
#include <asco/sync/notify.h>
#include <vector>
using namespace asco;

future_spawn<void> waiter(notify &n, std::atomic<int> &counter) {
    co_await n.wait();
    counter.fetch_add(1, std::memory_order_acq_rel);
    co_return;
}

future<int> async_main() {
    notify n;
    std::atomic<int> counter{0};
    std::vector<future_spawn<void>> waiters;

    for (int i = 0; i < 3; ++i) {
        waiters.push_back(waiter(n, counter));
    }

    n.notify_all();
    for (auto &w : waiters) co_await w;

    co_return counter.load(std::memory_order_acquire) == 3 ? 0 : 1;
}
```

## 注意事项

- `notify` 不会记录历史通知；在没有等待者时调用 `notify_one()` / `notify_all()` 会被直接丢弃。
- 若需要具备通知计数或超时语义，可考虑使用 `semaphore` 或其他同步原语。
