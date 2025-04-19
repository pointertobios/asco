# asco 异步运行时

## 运行时对象

CMake 工程中提供的目标 `asco-main` 中提供了 `main` 函数，其中构造了一个默认的 `asco::runtime` 对象，
并调用协程 `future<int> async_main()` ，对协程调用同步 `.await()` 。

## 工作线程

工作线程分为 `io worker` 和 `calculating worker` ，运行时初始化时将合适的工作线程绑定到某个 CPU 执行。

## 运行时对象配置

`asco::runtime` 构造函数仅有一个参数，用于设置工作线程数，参数默认值为0，为0时使用 `std::thread::hardware_concurrency()` 作为工作线程数。

未来会加入更多配置选项。

### 自定义运行时对象配置

在 `asco` 命名空间下提供了 `runtime_initializer_t`类型，
在你的代码中不属于任何命名空间的位置声明一个 `inline runtime_initializer_t runtime_initializer` 对象以使用自定义的配置：

```c++
inline asco::runtime_initializer_t runtime_initializer = [] () {
    std::cout << "custom runtime initializer" << std::endl;
    return new asco::runtime(4); // 创建一个只有4个工作线程的 `runtime` 对象
};
```

如需使用更自由灵活的自定义配置，或需要在 `main` 函数中编写其它代码，
可以不链接 CMake 工程提供的 `asco-main` 目标，自己编写 `main` 函数，并手动创建 `asco::runtime` 对象。

但是 `asco::runtime::sys` 中的功能是由 `asco-main` 目标提供的，因此若不链接 `asco-main` 目标，
需要参考 `asco-main` 目标的 `main` 函数手动实现或不使用 `asco::runtime::sys` 中的功能。

### 自定义运行时对象类

`asco` 提供了 `is_runtime<R>` 概念，自定义的运行时类必须符合该概念。
创建了自定义的运行时对象类后，使用如下代码为 `asco` 指定运行时对象类：

```c++
#include <asco/rutime.h>

#define SET_RUNTIME
set_runtime(MyRuntime);

#include <asco/future.h>
```

头文件的导入顺序非常重要，`<asco/runtime.h>` 只能在 `SET_RUNTIME` 宏定义之前导入，以使用 `set_runtime` 宏设置运行时类，
其余所有的头文件都必须在其后导入。
