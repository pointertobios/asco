# RWLock（协程读写锁）

`asco::sync::rwlock<>` 与 `asco::sync::rwlock<T>` 提供读多写独的协程同步原语。读操作可以并行获取共享锁，写操作以独占方式访问，被等待的协程会自动通过内部 `wait_queue` 挂起，不会阻塞线程。

- 头文件：`#include <asco/sync/rwlock.h>`
- 命名空间：在全局命名空间下别名为 `asco::rwlock`

## 类型概览

- `rwlock<>`：纯读写锁，不附带存储，仅管理并发访问。
- `template<typename T> rwlock<T>`：内部封装一个 `T` 实例，读锁以常量视图访问，写锁以可变引用操作对象。

两种读写锁都分别提供 `read()` 与 `write()`，返回 `future<read_guard>` 与 `future<write_guard>`，需配合 `co_await` 使用。

## 接口

### `rwlock<>`

- `future<read_guard> read()`：获取共享读锁。写者占用或排队时会自旋退避，随后挂起到读者等待队列 `rq`。
- `future<write_guard> write()`：获取独占写锁。尝试设置写意图位，等待所有读者释放并挂起到写者等待队列 `wq`。
- `class read_guard`
  - `operator bool() const noexcept`：指示守卫是否仍有效；移动后原守卫失效。
  - 析构时减少读者计数，当最后一个读者离开时唤醒写者。
- `class write_guard`
  - `operator bool() const noexcept`：移动后原守卫失效。
  - 析构时清除写标志，并唤醒排队写者与阻塞读者。

### `rwlock<T>`

- `future<read_guard> read()`：取得常量读守卫，可直接访问 `const T`。
- `future<write_guard> write()`：取得可写守卫，支持修改内部对象。
- `T &&get()`：在确认没有其他持有者时将内部值移动出来，后续任何 `read()` / `write()` 都会触发 panic，适合一次性转移所有权的场景。
- `class read_guard`
  - `const T &operator*() const` / `const T *operator->() const`：常量访问封装对象。
- `class write_guard`
  - `T &operator*()` / `T *operator->()`：可变访问封装对象。

## 行为特性

- **读写公平**：写协程在进入等待队列时会设置写意图，后续读者会挂起到 `rq` 等待写者释放，避免写者无限饥饿。
- **协程友好**：所有等待通过 `co_await` 表达，不会阻塞线程。内部结合指数退避与 `wait_queue`，在低争用时保持轻量，在高争用时自动挂起。
- **守卫语义**：守卫可移动但不可复制；移动后原对象变为无效状态（`operator bool()` 返回 `false`）。

## 示例

### 1. 在多个协程间共享配置快照

```cpp
#include <asco/future.h>
#include <asco/sync/rwlock.h>
#include <asco/utils/defines.h>
#include <string>
using namespace asco;

rwlock<std::string> config{"v1"};

future<void> update_config(std::string next) {
    with (auto guard = co_await config.write()) {
        *guard = std::move(next);
    }
    co_return;
}

future<std::string> read_config() {
    with (auto guard = co_await config.read()) {
        co_return *guard;  // 复制快照
    }
}
```

### 2. 读者并发、写者独占

```cpp
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/sync/rwlock.h>
#include <asco/utils/defines.h>
#include <vector>
using namespace asco;

rwlock<> resource_lock;

future_spawn<void> reader_task() {
    with (auto guard = co_await resource_lock.read()) {
        // 多个读者可以同时进入
    }
    co_return;
}

future_spawn<void> writer_task() {
    with (auto guard = co_await resource_lock.write()) {
        // 唯一写者，读者与其他写者都会等待
    }
    co_return;
}
```

## 注意事项

- 写守卫析构后会先唤醒其它写者，再唤醒读者；若需要严格保证写者优先，可在业务层面自行排队调度。
- `rwlock<T>::get()` 会永久标记内部对象已被移走，后续任意访问都会触发 panic，用于显式暴露误用。
- `rwlock<>` 不提供递归锁语义；请避免在持有写锁期间再次请求读锁或写锁。
- 推荐结合 `with` 宏使用，避免手动检查守卫有效性并确保作用域结束即释放锁。
