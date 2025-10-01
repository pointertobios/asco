# Async Synchronization Primitives

Concurrency often requires synchronization primitives to protect shared resources. A primitive itself is concurrency-safe and makes the protected resource concurrency-safe. This mirrors the notion of thread safety in multithreaded contexts.

ASCO provides a set of async-aware synchronization primitives designed for the ASCO runtime. Prefer these over raw OS syscalls or standard library blocking primitives: while waiting they suspend the coroutine, allowing the scheduler to run other coroutines instead of blocking the entire thread / process.

This improves CPU utilization: other processes only run when your process has no runnable coroutine or has exhausted its timeslice. This is a key efficiency advantage of coroutines over a naive multi-threaded blocking model.
