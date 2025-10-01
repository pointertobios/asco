# Async Generator `generator<T>`

`generator<T>` is a pull-based asynchronous sequence utility:

- Producer side: use `co_yield` inside a coroutine to produce elements incrementally.
- Consumer side: call `co_await g()` to pull a `std::optional<T>`; `std::nullopt` signals end-of-sequence (normal EOF).

Typical scenarios:

1. Incremental processing of large files / network streams (no need to load all in memory).
2. Composing / concatenating multiple upstream data sources.
3. Lazy (deferred) computation sequences.
4. Pipelines that may still throw after partially producing results (delayed exception rethrow).

Core types:

- `asco::generator<T>`: primary generator
- `asco::generator_core<T>`: internal core variant

The generator is itself the coroutine: producer uses `co_yield`; completion uses `co_return`; consumer loops with `while (g)` + `co_await g()`.

## Quick Example

```cpp
#include <asco/generator.h>
#include <asco/future.h>
using asco::generator;
using asco::future_inline;

generator<int> gen_count(int n) {
    for (int i = 1; i <= n; ++i) co_yield i;
    co_return;
}

future_inline<int> consume_sum(generator<int> &g) {
    int sum = 0;
    while (auto opt = co_await g()) {
        sum += *opt;
    }
    co_return sum;
}
```

## API & Semantics

Producer:

```cpp
generator<T> foo(...) {
    co_yield T{...};   // multiple times
}
```

Consumer for each `co_await g()`:

- If a value exists: returns `std::optional<T>` containing it
- If sequence normally ended: returns `std::nullopt`
- If producer threw an exception:
  - Already yielded values remain consumable in order
  - After all yielded values are drained, the next `co_await g()` rethrows the stored exception

Thus the exception surfaces only after buffered values are consumed — ensuring already produced data is not lost.

### Exception Timing

Internal flow:

1. Producer `co_yield` succeeds -> value pushed into a lock-free queue + semaphore signals waiting consumer.
2. Producer `return_void()` or exception path calls `yield_tx.stop()` closing the channel.
3. After close, `operator bool()` becomes false, but queued values may remain.
4. Consumer calling `g()` waits on semaphore; if an exception exists and no unconsumed values remain it is rethrown; else a value (or end) is returned.

## Lifecycle & Ownership

- Generator objects are movable, not copyable.
- After consumer sees `std::nullopt` it may destroy the generator safely; resources are reclaimed by the framework.

## Performance & Memory Traits

- Each `co_yield` is O(1) (queue push + atomic count + semaphore release).
- Large data: split into chunks for better pipelining.
- If producer outruns consumer heavily: queue may grow (no built-in backpressure). Solutions:
  1. Insert custom throttling awaits in producer.
  2. Let `T` be a lightweight handle (ref-count / span) instead of full heavy objects.

## Working with Timeouts / select / Abort

`generator<T>::operator()` returns an awaitable future; it composes with `select` / timeout helpers.
The generator itself is not timeout-aware; external selection logic decides to abort early by abandoning remaining pulls.

## Typical Patterns

1. File / socket chunk reader feeding a parser.
2. Multi-source merge: multiple generators raced via `select`.
3. Lazy math sequence scanning early-stop criteria.
4. Stream filter / map: wrap upstream, transform, and `co_yield` downstream.

## Composition Example: map

```cpp
template<class G, class F>
requires std::is_same_v<decltype(*std::declval<G&>()()), std::optional<typename G::value_type>>
auto map_gen(G upstream, F f) -> generator<decltype(f(std::declval<typename G::value_type&>()))> {
    while (auto v = co_await upstream()) {
        co_yield f(*v);
    }
}
```

## Backpressure & Scheduling

- Each `co_yield` is a fast path (likely `std::suspend_never` internally) and returns quickly.
- Consumer pacing determines throughput; explicit backpressure requires added awaits in the producer.

## I/O Chunk Read Example

```cpp
generator<buffer<>> read_chunks(file &f, size_t chunk_size) {
    while (auto r = co_await f.read(chunk_size)) {
        co_yield std::move(*r);
    }
}

future_inline<void> consume(file &f) {
    auto g = read_chunks(f, 4096);
    size_t total = 0;
    while (auto b = co_await g()) {
        total += b->size();
    }
}
```

### Relationship to `future<T>`

- A generator provides a stream of intermediate values, not a single result.
- Treat it as a one-way async channel: producer `co_yield`, consumer `co_await g()`.

## Best Practices

1. Always iterate as `while (auto v = co_await g()) { ... }`.
2. Distinguish normal end vs exception:

   ```cpp
   try {
       while (auto v = co_await g()) {
           // consume *v
       }
   } catch (const std::exception &e) {
       // only for real producer exception
   }
   ```

3. Move-only semantics — do not copy.
4. Yield heavy CPU loops periodically (`co_await asco::yield{}`).
5. For extremely hot consumption, batch or aggregate to reduce scheduling overhead.
