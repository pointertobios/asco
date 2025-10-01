# Asynchronous Programming with `future<T>`

`asco::future<T>` is a C++20 coroutine awaiter; it has no relation to `std::future<T>`.

As the return type of an async function it represents a value of type `T` that will be produced later. Callers either `co_await` it inside another async function or (from synchronous code) call `future<T>::await()` to block until the value is ready.

---

## Concepts

- Async Function: a function whose return type is `future<T>` or one of its variants.
- Coroutine: the coroutine frame object defined by the C++20 standard.
- Async Task: the entity scheduled by the asco async runtime consisting of the coroutine frame plus metadata/infrastructure. In special cases one async task can encompass multiple coroutines.

---

## Async Main Function

A function named `async_main` in the global namespace, taking no parameters and returning `asco::future<int>`, is the async main:

```cpp
#include <asco/future.h>

future<int> async_main() {
    // ...
    co_return 0;
}
```

After the async main returns the runtime immediately begins destruction; you cannot spawn new coroutines, but unfinished ones may still run to completion and clean up.

Fetch command-line arguments with `runtime::sys::args()` and environment variables with `runtime::sys::env()`[^1]:

```cpp
using asco::runtime::sys;
future<int> async_main() {
    for (auto &arg : sys::args()) {
        std::cout << arg << std::endl;
    }
    for (auto &[key, value] : sys::env()) {
        std::cout << key << " = " << value << std::endl;
    }
    co_return 0;
}
```

`asco_main` creates the runtime with default configuration[^1] and then calls `.await()` on the `async_main` future.

---

## Core Mechanics

When an asco async function is called, the task is enqueued to the runtime and a `future<T>` is returned immediately; execution does not start until the scheduler picks the task.

Inside an asco async function, using `co_await` suspends the current task until the awaited expression completes; suspended tasks are not scheduled.

When the `co_await` expression produces a result, the task resumes and awaits scheduling again.

Using `co_return` moves[^2] the return value to the caller and then suspends the task, which the runtime later cleans up.

---

## Variants of `future<T>`

### `future_inline<T>`

Behaves similarly to a lightweight `std::future`, but it is not enqueued into the runtime when created; instead the coroutine is immediately suspended. When the object is **co_await**ed, the coroutine resumes inline in the current context and runs to completion.

#### `dispatch()`

From a non-async (synchronous) environment you cannot call `.await()` directly on a `future_inline<T>`; first call `dispatch()` to transform it into a normal scheduled async task, then use `.await()`. You may also pass it forward through [task composition](./task_composition.md).

Calling `dispatch()` inside an async function throws a runtime exception.

### `future_core<T>`

Creates a core task: one that cannot be stolen and is preferentially sent to `calculating worker` threads[^1]. Suitable for CPU-bound work.

> On Intel hybrid processors with Hyper-Threading ("big/little" style), calculating workers run on performance cores; efficiency cores serve as IO workers. Future versions may map calculating workers to big cores on ARM big.LITTLE devices.

---

## `asco::yield`

Include `<asco/yield.h>`.

If you suspect the current coroutine might occupy the worker for a long period, use `co_await asco::yield{}` to yield the worker temporarily. The coroutine remains active; if no other active coroutine is ready, it may be scheduled again immediately.

---

## Passing References Between Coroutines

Automatic storage (stack) variables may reside in different places:

- If all accesses are before any suspension point, the variable can be optimized onto the worker thread stack.
- If accesses cross a suspension point, the variable lives inside the coroutine state object and is constructed/destructed with the coroutine.

Because coroutines may migrate to other workers, references to the former type (pure stack variables) must not be passed across coroutines. The same applies to `thread_local` variables: do not share references across coroutine boundaries.

Example:

```cpp
condition_variable decl_local(cv);
bool flag = false;
auto t = [&flag] -> future<void> {
    condition_variable coro_local(cv);
    co_await cv.wait([flag]{ return flag; });
    co_return;
}();
flag = true;
cv.notify_one();
co_await t;
```

> The macros `decl_local(cv)` and `coro_local(cv)` are safe; explained later.

Two scenarios:

- Same worker: when the parent coroutine suspends at `co_await t`, its stack frame unwinds; if `flag` was optimized onto the stack, the lambda's captured reference dangles.
- Different workers: if `flag` was on the stack of another thread, accessing it is undefined behavior per C++ since thread stack accessibility isn't guaranteed cross-thread.

---

## Error Handling

Standard `try/catch` works. Uncaught exceptions propagate to the caller and are rethrown when the caller `co_await`s or calls `.await()`.

`noexcept` is ignored—exceptions still propagate.

> The framework does not mandate any single error-handling strategy.
>
> Internally exceptions are for diagnostics; an internal throw likely indicates a bug or misuse.

### `asco::exception`

Header: `<asco/exception.h>`

Exception type carrying a stack trace plus async call-chain trace. Use or derive from this when throwing within the asco runtime. Constructor takes a `std::string` message; no need to override `what()`.

Constructing this outside the runtime throws `asco::runtime_error`.

Note: to capture correct addresses, signatures, and source locations for async call-chain tracing, disable optimizations ( -O0 ).

### `asco::runtime_error`

Header: `<asco/rterror.h>`

Exception with stack trace used internally.

---

## Sleeping

Sleep for a duration:

```cpp
future_inline<void> sleep_for(std::chrono::duration<Rep, Period>)
```

Any standard duration type (e.g. `nanoseconds`, `milliseconds`) and associated literals are supported.

Sleep until a time point:

```cpp
future_inline<void> sleep_until(std::chrono::time_point<Clock, Duration>)
```

---

## Coroutine Local Variables

Coroutine-local variables propagate along the call chain. A compile-time hashed name plus type check is used; lookup walks outward along the chain.

### Declaring with `decl_local(name, ...)` / `decl_local_array(name, ptr)`

```cpp
int decl_local(i);
i += 5;
std::string decl_local(str, new std::string("Hello ASCO"));
int *decl_local_array(arr, new int[10]);
```

### Access with `coro_local(name)`

```cpp
int *coro_local(arr);
std::string coro_local(str);
for (char c : str) {
    std::cout << c << ' ';
}
```

### Notes

- If a template parameter would normally be deduced automatically, you may need to specify it explicitly at the declaration site.
- For anonymous lambdas inside template parameters, specify the template argument explicitly to retain type validation.

---

## Abortable Coroutines

Call `.abort()` on a `future` to recursively abort that task and any subtask it is currently awaiting. If the task is suspended it is woken immediately; the resumed coroutine must handle the abort.

A coroutine must implement its own recovery logic on abort to restore state to what it was before execution (or cache obtained results for reuse).

If your coroutine lacks abort support, avoid using it in features that rely on abort semantics.

Most asco async functions are abortable, e.g. semaphore `.acquire()`:

```cpp
asco::binary_semaphore sem{1};
auto task = sem.acquire();
task.abort();
// acquire() returns future_inline<void>; must co_await to start
try { co_await task; } catch (coroutine_abort &) {}
assert_eq(sem.get_counter(), 1);
```

### Restoring Task State

Inside an asco async function call `bool this_coro::aborted()`; if true, run recovery logic (or reuse cached result) then immediately `throw coroutine_abort{}`. This is an abort decision point. Returning normally after detecting abort is undefined behavior.

The exception propagates to the caller after its `co_await`. If you do not catch it, you can still handle a child-task abort using `future<T>::aborted()`[^3].

Best practice: place an abort decision point before/after each suspension point[^4] and use RAII after `co_return` to ensure one final decision point.

After `co_return`, destructors cannot throw, but you can force the coroutine to throw via `this_coro::throw_coroutine_abort()`.

[^1]: See [asco Async Runtime](./advanced/runtime.md)
[^2]: Refers to `std::move()`; template parameter `T` must implement move construction and move assignment.
[^3]: Task abort handling; see [Task Composition](./task_composition.md)
[^4]: C++20 coroutine term: a `co_await`, `co_yield`, or `co_return`.
