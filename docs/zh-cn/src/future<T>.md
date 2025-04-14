# future\<T\> 协程函数下的异步编程

`asco::future<T>`是C++20 coroutine的一个等待器（ awaiter ），它与`std::future<T>`没有任何联系。

`asco::future<T>` （后简称 `future<T>` ）作为异步函数的返回值，表示该函数将在未来某个时刻返回一个`T`类型的值。
调用方可以在异步函数中使用 `co_await` ，或在同步函数中调用 `future<T>::await()` 等待异步函数返回并获取返回值。

---

## 异步主函数

使用宏 `asco_main` 标注名为 `async_main` 、没有形参、返回值为 `asco::future<int>` 的函数，则该函数成为异步主函数：

```c++
#include <asco/future.h>
asco_main future<int> async_main() {
    ...
    co_return 0;
}
```

使用 `runtime::sys::args()` 获取命令行参数， `runtime::sys::env()` 获取环境变量[^1]:

```c++
using asco::runtime::sys;
asco_main future<int> async_main() {
    for (auto& arg : sys::args()) {
        std::cout << arg << std::endl;
    }
    for (auto& [key, value] : sys::env()) {
        std::cout << key << " = " << value << std::endl;
    }
    co_return 0;
}
```

`asco_main` 使用默认配置[^1]创建异步 *asco 运行时*并对 `async_main` 函数的返回值调用 `.await()` 。

也可以自己编写 `main()` 函数对运行时进行特殊配置[^1]，但是无法使用 `runtime` 获取命令行参数和环境变量，必须自行从 `main()` 函数的参数读取。

---

## 核心机制

* 将任意一个返回`future<T>`的函数，称为 **asco 异步函数**。

**asco 异步函数**被调用时，立即将此函数作为一个任务发送给 *asco 异步运行时*并返回`future<T>`对象
，异步任务将不会立即开始执行，而是等待调度器调度。

**asco 异步函数**中使用 `co_await` 时，当前任务挂起，等待 `co_await` 表达式返回结果。任务挂起时，调度器不会调度此任务。

`co_await` 表达式返回结果时，当前任务恢复，等待调度器调度。

**asco 异步函数**中使用 `co_return` 时，将返回值***移动***[^2]给调用方，当前任务挂起并等待
*asco 异步运行时*稍后清理任务。

---

## future\<T\> 的变体

### future_inline\<T\>

`future_inline<T>` 的功能与 `std::future` 相同，但是它被创建时不会被发送给 *asco 异步运行时*，而是直接将协程挂起。
当此对象被 **co_await** 时，协程在当前上下文中被当场恢复，执行完毕后返回。

此等待器适用于本身十分短小但不得不执行异步代码的函数。

### future_blocking\<T\>

`future_blocking<T>` 的功能与 `std::future` 相同，但是它创建阻塞任务，
阻塞任务不可以被窃取且优先发送至 *calculating worker* 工作线程[^3]。

此等待器适用于 CPU 密集型任务。

> 在开启了超线程的 Intel 混合架构处理器（“大小核架构”）的 CPU 上， **calculating worker** 工作线程将运行在高性能核心（“大核”）上，
> 高能效核心（“小核”）均为 **io worker** 工作线程。
> 在未来，对于ARM big.LITTLE异构架构处理器（“大小核架构”）的安卓设备， **calculating worker** 工作线程将运行在大核上。

## 协程本地变量

**协程本地变量**沿调用链传播。使用基于编译期计算哈希值的类型检查和变量名查找，查找变量名时沿调用链一路向上搜索。

### 使用宏 `decl_local(name, ...)` 和 `decl_local_array(name, ptr)` 声明及初始化协程本地变量

```c++
int decl_local(i);
i += 5;
std::string decl_local(str, new std::string("Hello ASCO"));
int *decl_local_array(arr, new int[10]);
```

推荐使用 `new` 运算符而不是 `new []` 运算符构造变量。

### 使用宏 `coro_local(name)` 获取协程本地变量

```c++
int *coro_local(arr);
std::string coro_local(str);
for (char c : str) {
    std::cout << c << ' ';
}
```

[^1]: 见[asco 异步运行时](asco异步运行时.md)
[^2]: 指`std::move()`，模板参数 `T` 必须实现**移动构造函数**和**移动赋值运算符**。
[^3]: 见[asco 工作线程](asco工作线程.md)
