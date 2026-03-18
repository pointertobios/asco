# 周期 tick（interval）：`asco::time::interval`

本页介绍 `asco::time::interval`，用于在协程中以固定周期进行调度。

## 构造

`interval(duration)`

- `duration` 为周期长度（满足 `util::types::duration_type`）。

## 成员

`future<void> tick()`

## 语义

- 第一次 `tick()`：以构造时刻为基准；若调用时尚未到达“构造时刻 + duration”，则等待到该时间点。
- 第 `N` 次 `tick()` 的目标时间点始终是“构造时刻 + N × duration”。
- 若调用时已经超过该目标时间点，则本次 `tick()` 立即返回。
- 因此，当调用方长期未轮询时，后续若干次 `tick()` 可能连续立即返回，直到重新追上当前时间。
- 可被取消：当 `tick()` 内部正在等待下一次到期时，取消应使等待以取消方式结束。

## 何时使用

- 需要“尽快触发一次然后每隔 N 时间触发”：用 `interval`，并在循环里 `co_await it.tick()`。
