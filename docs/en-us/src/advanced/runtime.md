# asco Async Runtime

## Worker Threads

Two kinds of workers exist: `io worker` and `calculating worker`. During runtime initialization each worker is bound (affined) to a suitable CPU.

Inside asco a "blocking" task is defined as one that fully occupies its current worker so that other workers cannot steal it; this differs from the ordinary meaning of being paused waiting for I/O. Such tasks are preferentially scheduled onto `calculating worker`s.

To reduce user confusion when writing applications only against this framework we avoid the term "blocking" in user-facing docs and instead use the term `core`.

## Runtime Object Configuration

`asco::runtime` has a single constructor parameter: number of worker threads. Its default is 0 which means it will use `std::thread::hardware_concurrency()`.

More configuration options will be added in the future.
