# Semaphore

Header: `<asco/sync/semaphore.h>`

## Binary Semaphore (`asco::binary_semaphore`)

Holds either 0 or 1 indicating absence/presence of the protected resource.

### Construction

Must provide an initial value:

```cpp
binary_semaphore sem{0};
```

### Acquire

```cpp
future_inline<void> acquire()
```

Inline coroutine: `co_await` to start.

Behavior:

* If count == 1: set to 0 and return.
* If count == 0: suspend current coroutine and enqueue it.
* Abortable: if aborted before completion it won't set count to 0.

### Try Acquire

```cpp
bool try_acquire()
```

* If count == 1 -> set to 0 and return true.
* If count == 0 -> return false.

### Release

```cpp
void release()
```

Behavior:

* If count == 0: set to 1, wake one waiter, return.
* If count == 1: no-op.

## Counting Semaphore (`asco::semaphore<N>`)

Represents capacity of `N` resources.

Use `acquire()` / `release()`. `release(k)` releases `k` units (default 1). `k` must be positive:

```cpp
sem.release(2);
```

## Unlimited Semaphore (`asco::unlimited_semaphore`)

Alias:

```cpp
using unlimited_semaphore = semaphore_base<std::numeric_limits<size_t>::max()>;
```
