# Lock-Free Continuous Queue `continuous_queue`

Header: `<asco/nolock/continuous_queue.h>`

Provides a pair of producer/consumer handles: `sender<T>` and `receiver<T>`, created together via `create<T>()` and bound to the same internal frame (page) chain.
It is lock-free: enqueue/dequeue paths only use atomic operations and memory ordering primitives—no mutex is held. Fixed-size frames (cache line aligned) form a ring/linked buffer to reduce false sharing and cache jitter.

Suitable scenarios:

- High-throughput, low-latency single-sender + single-receiver messaging (also allows multiple copies of a sender/receiver handle to be used concurrently, but each individual handle object itself is not thread-safe).
- Situations where constructing/moving `T` is cheap and exception guarantees are solid.

Notes:

- `sender` and `receiver` are not themselves thread-safe, but you may copy handles to different threads (internally reference counts and atomic pointers manage frame list lifetime).
- `T` must satisfy a conceptual `move_secure` (move-safe) requirement.
- Constructors of `T` are, in principle, expected not to throw.

## Core Concepts

- `frame<T>`: 4KB-aligned fixed-size data page containing several cursors and state:
  - `head`: consumer progress (index already taken by receiver), initial preset 0.
  - `tail`: producer progress (reserved tail index); when full, set to `index_nullopt`.
  - `released`: producer-serialized release cursor ensuring write->visibility order.
  - `next`: singly-linked pointer to the next frame.
  - `sender_stopped` / `receiver_stopped`: flags for graceful stop.
  - `refcount`: shared refcount securing lifetime across handles and frame hops.
- Each frame holds `length = (4096 - header_size) / sizeof(T)` elements; element area aligned to `alignof(T)`.

## Interface Overview

- `std::tuple<sender<T>, receiver<T>> create<T>()`: create a paired set of handles.
- `sender<T>::push(T|T&&) -> std::optional<T>`:
  - On success returns `std::nullopt`.
  - If queue is closed or one side stopped, returns the unconsumed value so caller can recover it.
- `receiver<T>::pop() -> std::expected<T, receiver::pop_fail>`:
  - Success -> element value.
  - Failure -> `pop_fail::non_object` (temporarily empty) or `pop_fail::closed` (queue closed).
- `sender::stop()` / `receiver::stop()`: set stop flags across the frame chain and release currently held frame reference.
- `is_stopped()`: query whether this end considers the channel stopped/closed (including peer propagation).

## Example

```cpp
#include <asco/nolock/continuous_queue.h>

namespace cq = asco::continuous_queue;

void producer_consumer_demo() {
    auto [tx, rx] = cq::create<int>();

    // Producer side
    for (int i = 0; i < 1000; ++i) {
        if (auto unconsumed = tx.push(i)) {
            // Queue closed or peer stopped
            break;
        }
    }
    tx.stop(); // optional explicit end

    // Consumer side
    while (true) {
        auto r = rx.pop();
        if (r) {
            int v = *r;
            // use v
        } else if (r.error() == decltype(rx)::pop_fail::non_object) {
            // temporarily empty: spin / yield / sleep
            continue;
        } else {
            // closed
            break;
        }
    }
}
```

## Behavior & Memory Model

- push path:
  1. CAS increment `tail`; if full, attempt frame hop (ensure `next` exists; allocate/link if missing).
  2. Placement-construct element at computed index.
  3. Wait until `released == index`, then set `released = index + 1` to serialize production visibility.
- pop path:
  1. CAS increment `head`; if `head` reaches `length` attempt hop to `next` frame.
  2. If no element yet, return `non_object`; if stop detected and no more elements, return `closed`.
  3. Move/copy element out then explicitly destroy slot.
- stop propagation: `stop()` marks `sender_stopped` or `receiver_stopped` along frame chain; the opposite end, upon seeing the flag and no pending elements, treats queue as closed.

Memory ordering:

- Critical atomics use acquire/release or acq_rel to ensure cross-core visibility.
- Use `std::hardware_destructive_interference_size` alignment for key fields to reduce false sharing.

## Complexity & Performance Notes

- push/pop O(1); frame transitions incur minor allocation/link overhead (with a freelist to mitigate).
- Single-producer / single-consumer ideal locality; multi-sender/receiver remains lock-free but mild contention spinning may occur.
- For ultra-low latency, push spins briefly waiting for `released`; after a threshold uses `cpu_relax()`. Consider adding an upper-level yield/sleep strategy if appropriate.

## Correctness & Limitations

- `T`'s destructor should not throw during queue destruction path.
- Alignment constraints for `T` are satisfied by frame header layout (length > 16 implies size/align constraints hold).
- Sender/receiver handle objects themselves are not thread-safe; copy handles for multi-threaded use.
- No bounded backpressure: frames expand to a next frame when full; implement throttling externally if required.

## Relationship to the asco Runtime

Pure userspace; no dependence on scheduler or blocking syscalls. Works well as a high-efficiency message conduit inside asco coroutine scheduling, and equally usable in plain threads.

## API Reference

Namespace:

- `asco::nolock::continuous_queue` (implementation)
- `asco::continuous_queue` (re-export alias)

Types:

- `sender<T>`: producer side
- `receiver<T>`: consumer side (`pop_fail { non_object, closed }`)

Functions:

- `create<T>() -> tuple<sender<T>, receiver<T>>`
- `sender::push(T|T&&) -> optional<T>`
- `receiver::pop() -> expected<T, pop_fail>`
- `sender::stop() / receiver::stop()`
- `sender::is_stopped() / receiver::is_stopped()`

## Debugging & Troubleshooting

- Frequent `non_object`: production/consumption rate mismatch — consider adaptive yielding on consumer.
- Large `T` reduces per-frame capacity & locality; prefer passing pointers/handles instead of large objects.
