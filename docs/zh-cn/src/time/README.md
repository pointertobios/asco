# 时间与计时器（asco::time）

`asco::time` 提供基于单调时钟的“睡眠（sleep）”与“周期 tick（interval）”工具，用于在协程中进行延时与周期调度。

- 睡眠：见 [sleep：`sleep_until` 与 `sleep_for`](./sleep.md)
- 周期：见 [interval：`asco::time::interval`](./interval.md)

## 时间基准

- 计时基于 `std::chrono::steady_clock`（单调递增，不受系统时间校准影响）。
- 所有等待接口语义上都以“目标时间点是否已到”为准：如果目标已过去，应当立即完成而不是等待。

## 使用建议

- 需要“尽快触发一次然后每隔 N 时间触发”：使用 `interval`，并在循环里 `co_await it.tick()`。
- 需要“延迟到某个绝对时间点”：使用 `sleep_until`。
- 需要“延迟一段相对时间”：使用 `sleep_for`。
