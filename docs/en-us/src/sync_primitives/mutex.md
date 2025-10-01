# Mutex

Header: `<asco/sync/mutex.h>`

Acquisition and release use an RAII guard: call `.lock()` to obtain a guard; when the guard leaves scope the lock is released automatically.

The `lock` coroutine returns `future_inline<mutex<T>::guard>`; you must `co_await` it to begin acquisition.

The guard acts like an iterator/reference wrapper: dereference with `*` or `->` to access the underlying object.

```cpp
future<void> foo() {
    mutex<int> coro_local(muti);
    auto g = co_await muti.lock();
    *g = 5;
    std::cout << "foo *g: " << *g << std::endl;
    co_return;
}

future<int> async_main() {
    mutex<int> decl_local(muti);
    future<void> t;
    {
        auto g = co_await muti.lock();
        t = foo();
        std::cout << "*g: " << *g << std::endl;
    }
    co_await t;
    co_return 0;
}
```

## Constructors

* `mutex(const T&)` – copy a `T` into the mutex.
* `mutex(T&&)` – move a `T` into the mutex.
* `template<class... Args> mutex(Args&&... args)` – perfect-forward to in-place construct `T`.
