# future\<T\>

`asco::future<T>`是C++20 coroutine的一个等待器（ awaiter ），它与`std::future<T>`没有任何联系。

`asco::future<T>` （后简称 `future<T>` ）作为异步函数的返回值，表示该函数将在未来某个时刻返回一个`T`类型的值。
调用方可以在异步函数中使用 `co_await` ，或在同步函数中调用 `future<T>::await()` 等待异步函数返回并获取返回值。

该类型默认创建的是**非阻塞**任务，调度器可以在工作线程之间窃取**非阻塞**任务；相反，**阻塞**任务则不能被窃取。

## 异步主函数

使用宏 `asco_main` 标注名为 `async_main` 、没有形参、返回值为 `asco::future<int>` 的函数，则该函数成为异步主函数：

```c++
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

## 详细描述

* 将任意一个返回`future<T>`的函数，称为 **asco 异步函数**。

**asco 异步函数**被调用时，立即将此函数作为一个任务发送给 *asco 异步运行时*并返回`future<T>`对象
，异步任务将不会立即开始执行，而是等待调度器调度。

**asco 异步函数**中使用 `co_await` 时，当前任务挂起，等待 `co_await` 表达式返回结果。任务挂起时，调度器不会调度此任务。

`co_await` 表达式返回结果时，当前任务恢复，等待调度器调度。

**asco 异步函数**中使用 `co_return` 时，将返回值***移动***[^2]给调用方，当前任务挂起并等待
*asco 异步运行时*稍后清理任务。

## 实现细节

`future<T>`的完整声明如下：

```c++
template<typename T, typename R = RT>
requires is_move_secure_v<T> && is_runtime<R>
struct future {
    static_assert(!std::is_void_v<T>, "Use asco::future_void instead.");

    ...
};
```

对模板参数 `T` 的约束 `is_move_secure_v<T>` 定义如下：

```c++
template<typename T>
constexpr bool is_move_secure_v = 
    (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
        || std::is_integral_v<T> || std::is_floating_point_v<T>
        || std::is_pointer_v<T> || std::is_void_v<T>;
```

模板参数 `T` 要么同时实现了**移动构造函数**和**移动赋值函数**，要么是**数字**、**指针**或`void`。

模板参数 `R` 默认为 `asco::runtime` ，可以在引入 `<asco/future.h>` 前编写如下代码设置自定义的
*asco 异步运行时*：

```c++
#define SET_RUNTIME
set_runtime(<你的自定义异步运行时>);
```

*你的自定义异步运行时*必须符合 `asco::is_runtime<R>` 概念[^1]。

[^1]: 见[asco 异步运行时](asco异步运行时.md)
[^2]: 指`std::move()`，模板参数 `T` 必须实现**移动构造函数**和**移动赋值运算符**。
