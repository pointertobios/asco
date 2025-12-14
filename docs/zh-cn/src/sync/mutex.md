# Mutex（协程互斥锁）

`asco::sync::mutex<>` 与 `asco::sync::mutex<T>` 提供协程友好的互斥保护能力。它们以 `future` 形式返回守卫对象，利用 RAII 在离开作用域后自动释放锁，避免线程级别阻塞。

- 头文件：`#include <asco/sync/mutex.h>`
- 命名空间：在全局命名空间下别名为 `asco::mutex`

## 类型概览

- `mutex<>`：纯互斥锁，不携带值，仅提供排他访问。
- `template<typename T> mutex<T>`：在互斥锁内部封装一个 `T` 实例，锁定后可通过守卫直接访问 `T`。

两种互斥锁的 `lock()` 都返回 `future<guard>`，需使用 `co_await` 获取守卫。

## 接口

### `mutex<>`

- `future<guard> lock()`：等待获得互斥锁。协程在竞争失败时会自旋退避，必要时挂起在内部 `wait_queue` 上。
- `class guard`
  - `guard::operator bool() const noexcept`：指示守卫是否仍持有锁，被移动后的守卫返回 `false`。
  - 析构时自动释放锁并唤醒等待者。

### `mutex<T>`

- `future<guard> lock()`：同上，但守卫允许直接操作封装的 `T`。
- `class guard`
  - `T &operator*()` / `T *operator->()`：访问被保护的对象。
  - 移动构造后，原守卫失效，新守卫继续持有锁。
- `T &&get()`：将内部对象移动出互斥锁，并在后续 `lock()` 时触发 panic，适用于需要一次性夺取所有权的场景。

## 推荐用法：`with` 宏

提供 `with` 宏（在 `asco/utils/defines.h` 中定义为 `if`）：

```cpp
#include <asco/sync/mutex.h>
#include <asco/utils/defines.h>
using namespace asco;

sync::mutex<> mtx;

future<void> do_work() {
    with (auto guard = co_await mtx.lock()) {
        // 成功获取锁后执行，作用域结束自动解锁
    }
    co_return;
}
```

`with` 在语义上等同于 `if (auto guard = ...; guard) { ... }`，保证代码结构清晰且不会遗漏解锁。

## 示例

### 1. 保护共享计数器

```cpp
#include <asco/future.h>
#include <asco/sync/mutex.h>
#include <asco/utils/defines.h>
#include <atomic>
using namespace asco;

sync::mutex<> counter_mutex;
int counter = 0;
std::atomic<int> active{0};
std::atomic<int> violations{0};

future_spawn<void> worker() {
    with (auto guard = co_await counter_mutex.lock()) {
        auto prev = active.fetch_add(1, std::memory_order_acq_rel);
        if (prev != 0) {
            violations.fetch_add(1, std::memory_order_acq_rel);
        }
        ++counter;
        active.fetch_sub(1, std::memory_order_acq_rel);
    }
    co_return;
}
```

### 2. 保护对象并原地修改

```cpp
#include <asco/future.h>
#include <asco/sync/mutex.h>
#include <asco/utils/defines.h>
#include <string>
using namespace asco;

sync::mutex<std::string> name{"guest"};

future<void> rename(std::string new_name) {
    with (auto guard = co_await name.lock()) {
        *guard = std::move(new_name);
    }
    co_return;
}

future<std::string> snapshot() {
    with (auto guard = co_await name.lock()) {
        co_return *guard;  // 复制守卫中的字符串
    }
}
```

### 3. 彻底移交对象所有权

```cpp
#include <asco/future.h>
#include <asco/sync/mutex.h>
using namespace asco;

sync::mutex<std::vector<int>> data;

future<std::vector<int>> take_all() {
    with (auto guard = co_await data.lock()) {
        co_return data.get();  // 移动出内部向量
    }
}

future<void> reuse() {
    co_await take_all();
    // 再次锁定将触发 panic，提醒调用者对象已被移出
    // co_await take_all(); // panic
    co_return;
}
```

## 注意事项

- 守卫是可移动但不可复制的；请勿在移动后继续使用旧守卫。
- 如果 `mutex<T>::get()` 被调用，内部会标记对象已被移动；继续 `lock()` 会触发 panic，用于显式暴露错误路径。
- 互斥锁内部使用自旋退避与 `wait_queue` 协程挂起组合：在低争用场景中开销仅为原子操作，在高争用场景能避免忙等。
- `mutex<>` 只保障互斥，不提供递归锁语义；请避免在持锁期间再次调用 `lock()`。
