# `sync::condition_variable`：条件变量

`sync::condition_variable` 用于在多个异步任务之间表达“条件未满足就等待，条件变化后由别的任务通知”。

它只维护等待队列；**不绑定互斥锁，也不拥有被保护的数据**。因此，条件本身应保存在调用方自己的共享状态里，并由调用方负责并发安全。

头文件：`asco/sync/condition_variable.h`

---

## 1. 基本模型

典型模式是：

- 等待方：反复检查 predicate；若条件不成立，则挂起等待通知；
- 通知方：先更新共享状态，再调用 `notify_one()` / `notify(n)`，或使用带 predicate 的通知重载做额外门控。

示例：

```cpp
#include <atomic>
#include <asco/future.h>
#include <asco/sync/condition_variable.h>

using namespace asco;

sync::condition_variable cv;
std::atomic_bool ready{false};

future<void> waiter() {
    co_await cv.wait([&]() { return ready.load(std::memory_order::acquire); });
    // 这里只有在 ready == true 时才会继续
    co_return;
}

future<void> notifier() {
    ready.store(true, std::memory_order::release);
    cv.notify_one();
    co_return;
}
```

---

## 2. `wait(predicate)`：等待直到条件成立

```cpp
co_await cv.wait([&]() { return ready.load(std::memory_order::acquire); });
```

语义：

- `wait(...)` 返回 `future<void>`，需要在 ASCO runtime 上下文中 `co_await`。
- 每次进入等待循环时，都会先执行一次 predicate：
  - 若返回 `true`：立即返回，不进入等待队列；
  - 若返回 `false`：当前任务进入等待队列，直到被通知后再次检查 predicate。
- 被通知后，`wait(...)` 会再次检查 predicate；只有条件成立时才返回。

这意味着：

- `wait(...)` 适合表达“我要一直等到条件真的成立”；
- 调用 `notify_*()` 只是唤醒等待方，**不等于条件一定已经满足**；最终是否返回，仍由 predicate 决定。

---

## 3. `wait_once(predicate)`：最多等待一次通知

```cpp
bool suspended = co_await cv.wait_once([&]() { return ready.load(std::memory_order::acquire); });
```

返回值语义：

- 返回 `false`：predicate 一开始就成立，因此没有挂起；
- 返回 `true`：predicate 一开始不成立，当前任务确实挂起过一次，并在收到一次通知后恢复执行。

与 `wait(...)` 的关键区别：

- `wait_once(...)` 在恢复后**不会再次检查 predicate**；
- 它更像一个低层原语：告诉你“这次是否挂起过一次”，而不是“条件现在是否已经成立”。

因此，若你的需求是“直到条件成立才继续”，优先使用 `wait(...)`；只有在你明确要自己控制重试逻辑时，才使用 `wait_once(...)`。

---

## 4. 通知接口

### 4.0 `notify_failed`

带 predicate 的通知重载在失败时返回 `std::expected<..., notify_failed>`，错误码含义如下：

- `notify_failed::predicate_false`：通知侧的 predicate 返回 `false`，因此这次通知被拒绝，没有唤醒任何等待方。
- `notify_failed::no_waiters`：当前没有等待方，因此没有执行通知。

### 4.1 `notify_one()`

```cpp
bool woke = cv.notify_one();
```

语义：

- 若当前存在等待方：唤醒一个，并返回 `true`；
- 若等待队列为空：返回 `false`。

### 4.2 `notify_one(predicate)`

```cpp
auto result = cv.notify_one([&]() { return ready.load(std::memory_order::acquire); });
```

语义：

- 返回 `std::expected<void, notify_failed>`。
- 若没有等待方：返回 `std::unexpected{notify_failed::no_waiters}`。
- 若有等待方但 predicate 返回 `false`：返回 `std::unexpected{notify_failed::predicate_false}`，且不会唤醒等待方。
- 若有等待方且 predicate 返回 `true`：唤醒一个等待方，并返回成功结果。

### 4.3 `notify(n)` 与 `notify()`

```cpp
std::size_t woke = cv.notify(3);
std::size_t all = cv.notify();
```

语义：

- 唤醒最多 `n` 个等待方；
- 返回值为“本次实际唤醒的数量”；
- 当等待方数量少于 `n` 时，只会唤醒现有等待方。
- `notify()` 等价于“尽可能唤醒全部等待方”。

### 4.4 `notify(predicate, n)` 与 `notify(predicate)`

```cpp
auto result = cv.notify([&]() { return ready.load(std::memory_order::acquire); }, 3);
auto all = cv.notify([&]() { return ready.load(std::memory_order::acquire); });
```

语义：

- 返回 `std::expected<std::size_t, notify_failed>`。
- 若没有等待方：返回 `std::unexpected{notify_failed::no_waiters}`。
- 若有等待方但 predicate 返回 `false`：返回 `std::unexpected{notify_failed::predicate_false}`，且不会唤醒等待方。
- 若 predicate 返回 `true`：唤醒最多 `n` 个等待方，并返回实际唤醒数量。
- `notify(predicate)` 等价于“在 predicate 允许时，尽可能唤醒全部等待方”。

---

## 5. 与 `std::condition_variable` 的区别

`sync::condition_variable` 和传统线程条件变量的使用习惯相似，但有两个重要区别：

- 它是异步的：等待通过 `co_await` 完成，而不是阻塞线程；
- 它不与外部 mutex 绑定：共享状态的保护和可见性由调用方自己负责。

因此，predicate 最好只读取：

- 原子变量；或
- 已由其它同步原语安全保护的状态。

---

## 6. 使用建议

- 先更新共享状态，再调用 `notify_*()`；不要先通知后改状态。
- 需要“直到条件成立”为止时，使用 `wait(...)`。
- 需要“如果条件不成立，就先睡一次，醒来后我自己决定下一步”时，使用 `wait_once(...)`。
- 若多个等待方共享同一个条件，使用 `notify(n)` 控制一次唤醒多少个任务。
- 若通知本身也需要门控，使用带 predicate 的 `notify_*()` 重载；predicate 应尽量短小、无阻塞、无副作用。
- 当前实现尚未提供任务取消支持；设计等待协议时，需要考虑任务可能长期等待的路径。
