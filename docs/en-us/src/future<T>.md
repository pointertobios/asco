# future\<T\>

* LLM translated it from Chinese.

`asco::future<T>` is an awaiter for C++20 coroutines and is not related to `std::future<T>` in any way.

`asco::future<T>` (hereinafter referred to as `future<T>`) serves as the return value of asynchronous functions,
indicating that the function will return a value of type `T` at some future time.
Callers can use `co_await` within asynchronous functions or call `future<T>::await()` in synchronous functions
to wait for the asynchronous function to return and obtain the result.

This type creates **non-blocking** tasks by default.
The default scheduler will allocate time slices to alternate task execution.

## Detailed Description

* Any function returning `future<T>` is called an **asco asynchronous function**.

When an **asco asynchronous function** is called,
it immediately sends the function as a task to the *asco asynchronous runtime* and returns a `future<T>` object.
The asynchronous task will not start executing immediately, but will wait for scheduler dispatching.

When using `co_await` in an **asco asynchronous function**,
the current task is suspended until the `co_await` expression returns a result.
While suspended, the scheduler will not dispatch this task.

When the `co_await` expression returns a result, the current task resumes and waits for scheduler dispatching.

When using `co_return` in an **asco asynchronous function**, the return value is ***moved***[^1] to the caller.
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

The template parameter `R` defaults to `asco::runtime`. A custom *asco asynchronous runtime* can be set
by writing the following code before including `<asco/future.h>`:

```c++
#define SET_RUNTIME
set_runtime(<your custom asynchronous runtime>);
```

*Your custom asynchronous runtime* must satisfy the `asco::is_runtime<R>` concept[^2].

[^1]: Refers to `std::move()`. The template parameter `T` must implement both **move constructor** and **move assignment operator**.
[^2]: See [asco asynchronous runtime](asco异步运行时.md)
