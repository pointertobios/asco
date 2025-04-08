# future\<T\>

* LLM translated it from Chinese.

`asco::future<T>` is an awaiter for C++20 coroutines and has no relation to `std::future<T>`.

`asco::future<T>` (hereinafter referred to as `future<T>`) serves as the return value for asynchronous functions,
indicating that the function will return a value of type `T` at some future time.
Callers can use `co_await` in asynchronous functions
or call `future<T>::await()` in synchronous functions to wait for and retrieve the return value.

By default, this type creates **non-blocking** tasks.
The default scheduler can steal **non-blocking** tasks between worker threads, while
the **blocking** tasks cannot be stolen.

## Asynchronous Main Function

Using the macro `asco_main` to annotate a function named `async_main` with no parameters
and a return type of `asco::future<int>`, whitch makes it an asynchronous main function:

```c++
asco_main future<int> async_main() {
    ...
    co_return 0;
}
```

Use `runtime::sys::args()` to retrieve command-line arguments
and `runtime::sys::env()` to access environment variables[^1]:

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

The `asco_main` macro creates an asynchronous *asco runtime*
with default configurations[^1] and calls `.await()` on the return value of `async_main`.

You can also manually write a `main()` function with custom runtime configurations[^1],
but you cannot use `runtime` to retrieve command-line arguments or environment variables.
They must be manually read from the parameters of the `main()` function.

## Detailed Description

* Any function returning `future<T>` is called an **asco asynchronous function**.

When an **asco asynchronous function** is called, it immediately sends the function as a task
to the *asco asynchronous runtime* and returns a `future<T>` object.
The asynchronous task will not execute immediately but will wait for scheduler dispatching.

When `co_await` is used in an **asco asynchronous function**, the current task is suspended,
waiting for the `co_await` expression to return a result.
While suspended, the scheduler will not dispatch this task.

When the `co_await` expression returns a result,
the current task resumes and waits for scheduler dispatching.

When `co_return` is used in an **asco asynchronous function**,
the return value is ***moved***[^2] to the caller.
The current task is suspended and waits for the *asco asynchronous runtime* to clean up the task later.

## Implementation Details

The complete declaration of `future<T>` is as follows:

```c++
template<typename T, typename R = RT>
requires is_move_secure_v<T> && is_runtime<R>
struct future {
    static_assert(!std::is_void_v<T>, "Use asco::future_void instead.");

    ...
};
```

The constraint `is_move_secure_v<T>` for template parameter `T` is defined as:

```c++
template<typename T>
constexpr bool is_move_secure_v = 
    (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
        || std::is_integral_v<T> || std::is_floating_point_v<T>
        || std::is_pointer_v<T> || std::is_void_v<T>;
```

The template parameter `T` must either implement both **move constructor** and **move assignment operator**,
or be a **numeric type**, **pointer**, or `void`.

The template parameter `R` defaults to `asco::runtime`. To configure a custom *asco asynchronous runtime*,
add the following code before including `<asco/future.h>`:

```c++
#define SET_RUNTIME
set_runtime(<your_custom_runtime>);
```

*Your custom asynchronous runtime* must conform to the `asco::is_runtime<R>` concept[^1].

[^1]: See [Asco Asynchronous Runtime](asco_async_runtime.md)
[^2]: Refers to `std::move()`. Template parameter `T` must implement **move constructor** and **move assignment operator**.
