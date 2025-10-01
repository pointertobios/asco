# Performance Metrics

Enable the build flag `ASCO_PERF_RECORD` to turn on performance recording.

At the beginning of an async function insert the macro `coro_perf();`, or at the beginning of a normal (non-coroutine) function insert `func_perf();` to time the function. (Normal functions currently only record total run time.)

After program termination statistics are printed, for example:

```raw
[ASCO] Flag 'ASCO_PERF_RECORD' is enabled, please disable it if you are building your program for releasing.

active  total   counter name
1ms     2ms     205     future_inline<void> asco::sync::semaphore_base<1>::acquire() [CounterMax = 1, R = asco::core::runtime]
0ms     0ms     2       future_inline<guard> asco::sync::mutex<int>::lock() [T = int]
```
