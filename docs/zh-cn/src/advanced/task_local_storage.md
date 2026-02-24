# 任务本地存储（Task-local storage）

Task-local storage（下文简称 TLS）用于为“单个任务（task）”保存一份上下文数据，并在该任务的整个协程调用链中随时访问。

它和 `thread_local` 的核心区别是：

- `thread_local` 绑定线程；`task_local` 绑定任务（由 ASCO runtime 调度执行）。
- `task_local` 在同一任务内跨 `co_await` / `yield()` 保持一致与可见。

典型用途：请求/追踪上下文、每任务统计、逻辑上的“隐式参数”等。

## 快速开始

### 1）定义 TLS 类型

asco 的 TLS 以“类型”为键：一个任务里只存放**一份** TLS 对象。
如果你需要多份数据，把它们聚合到一个结构体里即可。

```cpp
struct my_tls {
    int request_id{};
    std::string trace_id;
};
```

### 2）在 `spawn()` / `block_on()` 时提供初始值

创建任务时把 TLS 实例作为第二个参数传入：

```cpp
#include <asco/core/runtime.h>

auto h = asco::spawn(
    []() -> asco::future<void> {
        // ...
        co_return;
    },
    my_tls{.request_id = 42, .trace_id = "abc"});
```

如果不需要 TLS，直接使用 `spawn(fn)` 即可。

### 3）在任务内部访问与修改

在任务（协程）内部，通过 `asco::this_task::task_local<T>()` 获取 TLS 的**引用**：

```cpp
#include <asco/this_task.h>

auto &tls = asco::this_task::task_local<my_tls>();
tls.request_id += 1;
```

同一任务内多次调用 `task_local<T>()` 会返回同一对象的引用；写入会在后续 `co_await` / `yield()` 后仍然可见。

## 语义说明

### 同一任务内：引用稳定、跨挂起保持

在同一个任务中：

- `task_local<T>()` 返回的引用在该任务生命周期内保持稳定（同一对象）。
- 对 TLS 的修改在 `co_await` / `this_task::yield()` 之后仍然能读到。

### 不同任务之间：相互隔离、不会继承

TLS **不会从父任务自动继承到子任务**。

也就是说：父任务 `spawn()` 了一个子任务，子任务要想使用 TLS，必须在创建子任务时显式传入它自己的 TLS 初始值；子任务对 TLS 的修改不会影响父任务。

如果你希望“父子任务共享状态”，应显式使用共享对象（例如 `std::shared_ptr<state>`）并把它作为 TLS 的成员或直接作为任务参数传递。

### 类型必须匹配

TLS 是按类型存取的：

- 你在 `spawn(fn, tls_value)` 里传入的 TLS 类型是 `T`，那么任务内部必须用 `task_local<T>()` 访问。
- 如果使用了不同的类型访问，会触发断言失败（类型安全检查）。

因此，一个任务只会有一份 TLS：想存多种值，请把它们放进同一个结构体。

## 生命周期与析构时机

### 构造

TLS 对象由 `spawn()` / `block_on()` 的第二个参数初始化（以转发/移动方式构造）。

### 可访问范围

`task_local<T>()` 仅在“当前正在执行的任务”内部可用。

- 若当前没有正在运行的任务，`task_local<T>()` 会触发 panic（提示“当前没有正在运行的任务”）。
- 不要在 runtime 之外调用它。

### 析构

TLS 会在任务执行期间一直存在，并且至少存活到任务结束。

析构时机不做精确保证，但可以依赖以下语义：

- 若你在任务结束后仍持有对应的 `join_handle`，TLS 可能继续存活，从而延迟资源回收。
- 若你提前销毁 `join_handle`，TLS 也不会因此在任务尚未结束时提前析构。

建议：不要把 TLS 析构发生的“确切时刻”当作业务语义；若需要确定性的清理，应在任务函数内部显式管理资源生命周期。

## 常见模式与建议

- 把 TLS 当作“每任务上下文”，不要用它做跨任务共享。
- 需要传播上下文时，显式把上下文作为 `spawn()` 的第二个参数传给新任务。

## 最小示例

下面示例展示：初始值来自 `spawn()`，修改跨 `yield()` 保持，且同一任务内引用稳定。

```cpp
struct tls_int { int value; };

auto h = asco::spawn(
    []() -> asco::future<int> {
        auto &tls = asco::this_task::task_local<tls_int>();
        tls.value += 1;
        co_await asco::this_task::yield();
        co_return asco::this_task::task_local<tls_int>().value;
    },
    tls_int{41});

int result = co_await h; // result == 42
```
