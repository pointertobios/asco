# `sync::spinlock`：自旋锁

`sync::spinlock` 提供线程间互斥：同一时刻最多只有一个执行流持有锁。

它适用于：

- 临界区很短、竞争不激烈的场景。

头文件：`asco/sync/spinlock.h`

---

## 1. `spinlock<>`（无数据）

```cpp
#include <asco/sync/spinlock.h>

asco::sync::spinlock<> lock;

{
    auto g = lock.lock();
    // 临界区
}
// g 析构后自动解锁
```

语义：

- `lock.lock()` 会获取锁，并返回一个守卫对象（guard）。
- guard 的生命周期内锁处于持有状态；guard 析构时解锁。

---

## 2. `spinlock<T>`（保护一个值）

`spinlock<T>` 用于把某个值 `T` 与一把锁绑定在一起。

```cpp
#include <asco/sync/spinlock.h>
#include <vector>

asco::sync::spinlock<std::vector<int>> xs;

{
    auto g = xs.lock();
    g->push_back(1);
    g->push_back(2);
}
```

语义：

- `xs.lock()` 返回一个 guard。
- 通过 `*g` / `g->` 访问被保护的 `T`。

---

## 3. 使用建议

- 只在临界区很短时使用自旋锁；临界区越长，对其它执行流的影响越大。
- 不要在持有自旋锁期间执行可能长时间运行的操作（例如长循环、阻塞调用）。
- 不要在持有自旋锁期间跨越 `co_await`；建议在 `co_await` 前释放锁，恢复后再重新获取。
