# Read-Write Lock

Header: `<asco/sync/rwlock.h>`

Acquire via `.read()` or `.write()` returning guards. Guards auto-release on scope exit.

`read` returns `future_inline<rwlock<T>::read_guard>`
`write` returns `future_inline<rwlock<T>::write_guard>`

```cpp
future<int> async_main() {
    rwlock<int> lk{10};
    {
        auto g1 = co_await lk.read();
        auto g2 = co_await lk.read();
        std::cout << *g1 << std::endl;
        std::cout << *g2 << std::endl;
        // auto g3 = co_await lk.write();  // cannot get it
    }
    {
        auto g = co_await lk.write();
        *g = 20;
        std::cout << *g << std::endl;
        // auto g1 = co_await lk.write();  // cannot get it
        // auto g1 = co_await lk.read();   // cannot get it
    }
    co_return 0;
}
```

## Constructors

* `rwlock(const T&)` – copy a `T`.
* `rwlock(T&&)` – move a `T`.
* `template<class... Args> rwlock(Args&&... args)` – perfect-forward in-place construction.
