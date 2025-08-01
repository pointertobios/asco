# future\<T\> 协程下的异步编程

`asco::future<T>`是C++20 coroutine的一个等待器（ awaiter ），它与`std::future<T>`没有任何联系。

`asco::future<T>` （后简称 `future<T>` ）作为异步函数的返回值，表示该函数将在未来某个时刻返回一个`T`类型的值。
调用方可以在异步函数中使用 `co_await` ，或在同步函数中调用 `future<T>::await()` 等待异步函数返回并获取返回值。

---

## 概念

* 异步函数：指返回值为 `future<T>` 或其变体的函数定义。
* 协程：指 C++20 协程标准所定义的“协程帧”对象。
* 异步任务：除“协程帧”对象外，还有其相关的元数据和协程基础设施构成的被 *asco 异步运行时*所调度的实体。
  特殊情况下，一个异步任务也可以对应多个协程。

---

## 异步主函数

在全局命名空间中、名为 `async_main` 、没有形参、返回值为 `asco::future<int>` 的函数是异步主函数：

```c++
#include <asco/future.h>

future<int> async_main() {
    ...
    co_return 0;
}
```

异步主函数返回后运行时立即销毁，无法创建新协程，但是未完成的协程可以正常运行和销毁。

使用 `runtime::sys::args()` 获取命令行参数， `runtime::sys::env()` 获取环境变量[^1]:

```c++
using asco::runtime::sys;
future<int> async_main() {
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

---

## 核心机制

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

#### dispatch() 函数

当在非异步环境中调用 `future_inline<T>` 的异步函数时，不能使用 `await()` 阻塞等待其返回，
需要先调用 `dispatch()` 将其转化为非 inline 的异步任务后再使用 `await()`。也可以利用[异步任务组合](./任务组合.md)将此任务向后传递。

在异步函数环境中调用 `dispatch()` 时，将抛出一个运行时异常。

### future_core\<T\>

`future_core<T>` 的功能与 `std::future` 相同，但是它创建核心任务，
核心任务不可以被窃取且优先发送至 *calculating worker* 工作线程[^1]。

此等待器适用于 CPU 密集型任务。

> 在开启了超线程的 Intel 混合架构处理器（“大小核架构”）的 CPU 上， **calculating worker** 工作线程将运行在高性能核心（“大核”）上，
> 高能效核心（“小核”）均为 **io worker** 工作线程。
> 在未来，对于ARM big.LITTLE异构架构处理器（“大小核架构”）的安卓设备， **calculating worker** 工作线程将运行在大核上。

---

## `asco::yield`

包含头文件 `<asco/yield.h>` 即可使用。

当你认为当前协程可能长时间占用工作线程或任何可能需要让出工作线程以使其它协程得以执行时，使用 `co_await asco::yield{}`
以暂时让出工作线程，协程依然处于活动状态，若当前工作线程中没有其它可以调度的活动协程，当前协程依然会被继续调度。

---

## 有关协程之间引用的传递

协程的自动储存期变量根据不同情况有不同的储存位置：

* 变量的所有访问行为没有跨过任何协程暂停点，变量可能会被优化到当前工作线程的线程栈中。
* 变量的所有访问行为跨过了协程暂停点，变量会存在于协程状态对象中，随着协程的创建和销毁而构造和析构。

由于协程可能会分配到不同的工作线程中执行，前一种自动储存期变量的引用不可以在协程间传递。以 `thread_local` 关键字声明的变量与这种情况相同，
不可以将它的引用在协程间传递。

以这段代码为例：

```c++
condition_variable decl_local(cv);
bool flag = false;
auto t = [&flag] -> future_void {
    condition_variable coro_local(cv);
    co_await cv.wait([flag]{ return flag; });
    co_return {};
} ();
flag = true;
cv.notify_one();
co_await t;
```

> `decl_local(cv)`、`coro_local(cv)` 的使用是安全的，这两个宏将在后面的段落讲解。

变量 `flag` 没有跨过任何协程暂停点，因此它将在当前工作线程的线程栈中被分配。

讨论两种情况：

* 当前协程与 lambda 表达式在同一工作线程中被调度执行：当前协程执行至 `co_await t` 挂起后，线程栈退出当前栈帧，如果 `flag` 被优化到栈上，
  会失效， lambda 表达式中捕获的 `flag` 将引用一个有效但不合法的地址，是未定义行为。
* 当前协程与 lambda 表达式不在同一工作线程中被调度执行：如果 `flag` 被优化到栈上，
  lambda表达式中的变量 `flag` 引用了一个其它线程中的栈上地址，C++标准并未规定线程栈在整个进程中的可访问性，访问这个引用是未定义行为。

---

## 错误处理

支持使用 `try-catch` 捕获异常。

未捕获的异常将传递给调用方，在调用方 `co_await` 或 `.await()` 时抛出。

`noexcept` 会被忽略，以相同的方式传递异常。

本框架**不限制**其它错误处理方式的使用，也不提供其它错误处理方式的基础设施。

## asco::exception

`<asco/exception.h>`

自带堆栈追踪和异步函数调用链追踪的异常类，若需要在 *asco 异步运行时*中抛出带有堆栈追踪的异常，请直接使用或派生此类。

此类的构造函数接收一个 `std::string` 参数作为异常的 `what()` 信息，派生类无需自己重载 `const char *what() noexcept` 函数。

若此类没有在 *asco 异步运行时*中构造，将会抛出 `asco::runtime_error` 。

* 注：异步函数调用链追踪需要关闭优化( ***-O0*** )才能获取正确的地址、函数签名和源代码位置。

## asco::runtime_error

`<asco/rterror.h>`

自带堆栈追踪的异常类，用于运行时内部的异常处理。

---

## 协程睡眠

睡眠指定的时间间隔：

```c++
future_void_inline sleep_for(std::chrono::duration<Rep, Period>)
```

`duration` 类型包括标准库中任意的时间间隔类型如 `nanoseconds` 、 `miliseconds` 等，以及它们对应的字面值字符串运算符。

睡眠至指定的时间点：

```c++
future_void_inline sleep_until(std::chrono::time_point<Clock, Duration>)
```

---

## 协程本地变量

**协程本地变量**沿调用链传播。使用基于编译期计算哈希值的类型检查和变量名查找，查找变量名时沿调用链一路向上搜索。

### 使用宏 `decl_local(name, ...)` 和 `decl_local_array(name, ptr)` 声明及初始化协程本地变量

```c++
int decl_local(i);
i += 5;
std::string decl_local(str, new std::string("Hello ASCO"));
int *decl_local_array(arr, new int[10]);
```

### 使用宏 `coro_local(name)` 获取协程本地变量

```c++
int *coro_local(arr);
std::string coro_local(str);
for (char c : str) {
    std::cout << c << ' ';
}
```

### 注意

若变量类型的模板参数中具有自动推导的模板参数，其自动推导无法传递至开头的类型声明处，需要手动指定。

若在变量构造处变量类型的模板参数中具有可以自动推导的匿名 lambda 表达式，需要显式填入模板参数，否则类型验证会失效。

---

## 可打断协程

对 `future` 调用 `.abort()` 递归打断这个任务以及这个任务正在挂起等待的子任务，如果任务正在挂起，立即唤醒，被唤醒的协程应正确处理打断。

协程函数需要自己实现被打断时的恢复功能，以将状态恢复到协程开始执行前。

如果你的协程没有实现可打断特性，请谨慎使用于 *asco* 提供的依赖可打断特性的功能。

*asco* 内部大多数异步函数都具有可打断支持，如信号量的 `.acquire()` 函数：

```c++
asco::binary_semaphore sem{1};
auto task = sem.acquire();
task.abort();
// acquire() 返回 future_void_inline 类型，需要手动 co_await 使任务开始执行
try { co_await task; } catch (coroutine_abort &) {}
assert_eq(sem.get_counter(), 1);
```

### 恢复任务状态

在 *asco 异步函数*中调用 `bool this_coro::aborted()` ，返回 `true` 时执行状态恢复逻辑或缓存已得到的结果供下次调用时使用，
然后立即 `throw coroutine_abort{}` 。此处的代码称为**打断判定点**。如果没有抛出此异常直接返回，则是未定义行为。
此异常会继续在调用者 co_await 后抛出，若不使用 `try-catch` 捕获，还可以使用 `future<T>::aborted()`[^3] 对子任务被打断的情况进行处理。

***编写可打断协程最佳实践***：在每个**协程暂停点**[^4]前后设置一个打断判定点，并在 `co_return` 之后利用 `raii` 设置一个**打断判定点**。

在 `co_return` 后，析构阶段无法抛出异常，但是可以通过 `this_coro::throw_coroutin_abort()` 直接让协程抛出异常。

以本项目的 `channel::reveiver<T>::recv()` 为例：

可以看到，每个打断判定点外的 `co_return` 前都有一个打断判定点。

协程的自动储存期变量（通常所谓的**本地变量**，这里与**协程本地变量**作区分使用此名称）会在 `co_return` 后按照初始化相反的顺序析构。
因此，变量 `restorer` 的存在使得协程在返回后依然有机会判断是否被打断。

在每个 `co_return` 或 `throw coroutine_abort{}` 前，都设置 `restorer.state` 的值，因此，
`restorer` 的析构函数可以在不同的 `co_return` 或 `throw coroutine_abort{}` 后执行不同的恢复操作。

在此期间，可以使用 `T &&this_coro::move_back_return_value<future<T>>()` 将返回值移动回当前上下文以避免其被丢弃。

```c++
[[nodiscard("[ASCO] receiver::recv(): You must deal with the case of channel closed.")]]
future_inline<std::optional<T>> recv() {
    struct re {
        receiver *self;
        int state{0};

        ~re() {
            if (!this_coro::aborted())
                return;

            this_coro::throw_coroutine_abort<future_inline<std::optional<T>>>();

            switch (state) {
            case 2:
                self->buffer.push_back(
                    this_coro::move_back_return_value<future_inline<std::optional<T>>>());
            case 1:
                self->frame->sem.release();
                break;
            default:
                break;
            }
        }
    } restorer{this};

    if (none)
        throw asco::runtime_error(
            "[ASCO] receiver::recv(): Cannot do any action on a NONE receiver object.");
    if (moved)
        throw asco::runtime_error("[ASCO] receiver::recv(): Cannot do any action after receiver moved.");

    if (this_coro::aborted()) {
        restorer.state = 0;
        throw coroutine_abort{}
    }

    if (!buffer.empty()) {
        std::optional<T> res{std::move(buffer[0])};
        buffer.erase(buffer.begin());
        restorer.state = 2;
        co_return std::move(res);
    }

    co_await frame->sem.acquire();

    if (this_coro::aborted()) {
        frame->sem.release();
        restorer.state = 0;
        throw coroutine_abort{}
    }

    if (frame->sender.has_value()) {
        if (is_stopped()) {
            restorer.state = 1;
            co_return std::nullopt;
        }

        if (*frame->sender == *frame->receiver)
            throw asco::runtime_error(
                "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");

    } else if (*frame->receiver == FrameSize) {
        // go to next frame.
        auto *f = frame;
        if (!f->next)
            throw asco::runtime_error(
                "[ASCO] receiver::recv(): Sender went to next frame, but next frame is nullptr.");
        frame = f->next;
        delete f;
        frame->receiver = 0;

        co_await frame->sem.acquire();

        if (this_coro::aborted()) {
            frame->sem.release();
            restorer.state = 0;
            throw coroutine_abort{}
        }

        if (is_stopped()) {
            restorer.state = 1;
            co_return std::nullopt;
        }

        if (frame->sender && *frame->sender == *frame->receiver)
            throw asco::runtime_error(
                "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");
    }

    restorer.state = 2;
    co_return std::move(((T *)frame->buffer)[(*frame->receiver)++]);
}
```

[^1]: 见[asco 异步运行时](进阶/asco异步运行时.md)
[^2]: 指`std::move()`，模板参数 `T` 必须实现**移动构造函数**和**移动赋值运算符**。
[^3]: 任务打断处理，见[任务组合](./任务组合.md)
[^4]: C++20 coroutine 使用的术语，指 `co_await` 、 `co_yield` 、 `co_return` 。
