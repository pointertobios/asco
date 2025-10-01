# Task Composition

In real-world engineering, asynchronous tasks are often combined with ordering or racing relationships rather than executed strictly one-after-another with a simple await. ASCO provides several composition forms.

Except for `select<N>`, most composition helpers may also be invoked in a synchronous (inline) context.

---

## Non-Blocking Serial Chaining: `future::then()`

`future<T>::then()` chains the result of one asynchronous operation into the next, enabling serial composition without blocking. When the preceding `future` finishes, its result is forwarded to the provided asynchronous function, and a new `future` is returned representing the overall chained result.

It returns another `future` that can itself be composed further.

### Usage (exceptionally)

```cpp
future<int> f1 = some_async_func();
auto f2 = std::move(f1).then([](int result) -> future<std::string> {
    // Use result for subsequent async work
    co_return std::to_string(result);
});
```

`then()` takes a rvalue *deduced this*. Commonly you call `some_async_func().then(...)` directly; here we spelled out an intermediate variable for clarity. If the callable is a lambda its parameter must not be `auto` (no generic parameter deduction there).

### Behavior (exceptionally)

* Waits for the current future to finish, then passes its value to `f`.
* Returns a future representing the chained asynchronous result.
* Exceptions propagate: if the previous future throws, the exception arrives at the `then` result.

### Notes (exceptionally)

* Only supports chaining to an asynchronous function (i.e. the callable must return a `future`).
* Calling `then()` on a `future_inline` migrates execution onto the current worker (implementation detail explained elsewhere).

---

## Asynchronous Exception Handling: `future::exceptionally()`

`future<T>::exceptionally()` adds exception handling to a chain. If the previous future throws a specific exception type, the handler runs; if it throws something else the exception is rethrown into the `exceptionally` future.

### Usage

```cpp
future<int> f = some_async_func();
auto f2 = std::move(f).exceptionally([](const std::runtime_error& e) {
    std::cout << "caught: " << e.what() << std::endl;
});
```

The callable parameter type must be a specific exception type, a reference to it, or `std::exception_ptr` to catch any captured exception.

### Behavior

* Waits for the current future.
* If the current future returns normally, the resulting future returns the original value.
* If it throws and the type matches, the handler is invoked and the result becomes an `std::expected<T,E>` with an error (`std::unexpected`).
* If the type does not match, the exception propagates upward.
* Return type: `future<std::expected<T,E>>`, where `E` is deduced from the handler's return type (or `std::monostate` for `void`).

### Notes

* Handler can be lambda / function pointer / `std::function`.
* To catch all exceptions, use `std::exception` or `std::exception_ptr`.

### Coroutine Abort Exception

When a coroutine is aborted (e.g. via `future.abort()` or by a winning branch in `select<N>`), a `coroutine_abort` exception should be thrown. You have two ways to handle it:

1. Manual try/catch in the coroutine body:

   ```cpp
   try {
       // ...
   } catch (const coroutine_abort&) {
       // cleanup or logging
   }
   ```

2. Use `future<T>::aborted()` for a dedicated handler:

   ```cpp
   auto f = some_async_func().aborted([] {
       std::cout << "coroutine aborted" << std::endl;
   });
   ```

When aborted, the handler runs and returns `std::nullopt` (or an appropriate sentinel), otherwise the normal result is produced.

---

## Racing Composition (Selection) `select<N>`

`select<N>` races N coroutine branches, resuming only the first to complete and aborting the others.

```cpp
asco::interval in1s{1s};
asco::interval in500ms{500ms};
for (int i = 0; i < 6; ++i) {
    switch (co_await asco::select<2>{}) {
    case 0: {
        co_await in1s.tick();
        std::cout << "1s\n";
        break;
    }
    case 1: {
        co_await in500ms.tick();
        std::cout << "500ms\n";
        break;
    }
    }
}
```

After `co_await` on a constructed `select<N>` object, it returns a `size_t` index in clone order (0..N-1). The selector only governs the first asynchronous operation after the await.

The earliest finishing branch aborts the other branches. Even if a slower branch had logically completed, its side effects must adhere to the abortable semantics: state is rolled back or ignored according to the defined abort contract.

Aborted coroutines tear down their call chain. For a correct `select<N>` usage, exactly `N-1` cloned branches are aborted.

---

## Summary

| Pattern            | Core API                        | Purpose                                          |
| ------------------ | ------------------------------- | ------------------------------------------------ |
| Serial chaining    | `then()`                        | Pass result forward without blocking             |
| Exception handling | `exceptionally()` / `aborted()` | Transform or observe failures / abort events     |
| Racing             | `select<N>`                     | Pick the earliest finishing branch, abort others |

These primitives can be freely combined to express sophisticated asynchronous control flow while preserving clarity and abort semantics.
