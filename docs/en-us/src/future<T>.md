# Asynchronous Programming with Future\<T\> Coroutines

`asco::future<T>` is an awaiter for C++20 coroutines and has no relation to `std::future<T>`.

`asco::future<T>` (hereafter referred to as `future<T>`) serves as the return value
for asynchronous functions,
indicating that the function will return a value of type `T` at some future time.
Callers can use `co_await` in asynchronous functions or call `future<T>::await()`
in synchronous functions to wait for and retrieve the return value.

---

## Asynchronous Main Function

A function named `async_main` with no parameters and returning `asco::future<int>`,
when annotated with the macro `asco_main`, becomes an asynchronous main function:

```c++
#include <asco/future.h>
asco_main future<int> async_main() {
    ...
    co_return 0;
}
```

Use `runtime::sys::args()` to retrieve command-line arguments and `runtime::sys::env()`
to access environment variables[^1]:

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

The `asco_main` macro creates an *asco runtime* with default configurations[^1] and
calls `.await()` on the return value of `async_main`.

You may also manually write a `main()` function with custom runtime configurations[^1],
but in this case, you cannot use `runtime` to access command-line arguments or
environment variables and must read them directly from `main()`'s parameters.

---

## Detailed Behavior

- Any function returning `future<T>` is termed an **asco asynchronous function**.

When an **asco asynchronous function** is called, it is immediately sent as a task
to the *asco async runtime* and returns a `future<T>` object.
The asynchronous task does not execute immediately but waits for scheduler dispatching.

When `co_await` is used in an **asco asynchronous function**,
the current task is suspended until the `co_await` expression yields a result.
**While suspended, the scheduler does not schedule this task.**

When the `co_await` expression resolves, the current task resumes and waits for scheduler dispatching.

When `co_return` is used in an **asco asynchronous function**,
the return value is ***moved***[^2] to the caller.
The current task is suspended and awaits cleanup by the *asco async runtime*.

---

## Variants of future\<T\>

### future_inline\<T\>

`future_inline<T>` behaves similarly to `future<T>`,
but when created, it is **not** sent to the *asco async runtime*.
Instead, the coroutine is suspended immediately.
When this object is **co_awaited**,
the coroutine is resumed **inline** in the current context and executed to completion.

This awaiter is suitable for functions that are inherently short but must execute asynchronous code.

### future_blocking\<T\>

`future_blocking<T>` behaves similarly to `future<T>` but creates a **blocking task**.
Blocking tasks cannot be stolen and are prioritized for dispatch to **calculating worker** threads[^3].

This awaiter is designed for CPU-intensive tasks.

> On Intel Hybrid Architecture CPUs, with Hyper-Threading enabled,
> **calculating worker** threads run on P-cores, while E-cores are reserved for **io worker** threads.
> In the future, for ARM bit.LITTLE Android devices,
> **calculating worker** threads will run on big cores.

---

## **Coroutine-Local Variables**

Coroutine-local variables propagate along the call chain. The implementation uses **type checking** and **variable name lookup** based on **compile-time computed** hash values, where variable searches ascend upward through the call chain.

### Declaration & Initialization

Use the macro `decl_local(name, ...)` to declare and initialize coroutine-local variables:

```cpp
int decl_local(i);
i += 5;
std::string decl_local(str, new std::string("Hello ASCO"));
```

- Prefer the `new` operator over `new[]` for variable construction.

### Accessing Variables

Use the macro `coro_local(name)` to retrieve coroutine-local variables:

```cpp
std::string coro_local(str);
for (char c : str) {
    std::cout << c << ' ';
}
```

[^1]: See [asco Async Runtime](asco_async_runtime.md)
[^2]: Refers to `std::move()`. The template parameter `T` must implement a **move constructor** and **move assignment operator**.
[^3]: See [asco Worker Threads](asco_worker.md)
