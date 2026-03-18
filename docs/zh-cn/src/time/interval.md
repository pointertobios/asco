# 周期 tick（interval）：`asco::time::interval`

本页介绍 `asco::time::interval`，用于在协程中以固定周期进行调度。

## 构造

`interval(duration)`

- `duration` 为周期长度（满足 `util::types::duration_type`）。

## 成员

`future<void> tick()`

## 语义

- 第一次 `tick()`：以构造时刻为基准；若调用时尚未到达“构造时刻 + duration”，则等待到该时间点。
- 后续 `tick()`：
  - 若距离上一次 `tick()` 完成的时间未满一个周期，则等待到“上次完成时刻 + duration”。
  - 若调用时已经超过该目标时间点（例如调用方长期未轮询），则应立即返回。
- 不追赶（no catch-up）：当发生超时/延迟时，`tick()` 不会补发多个周期 tick；它只会基于“当前时刻”重新建立后续周期。
- 可被取消：当 `tick()` 内部正在等待下一次到期时，取消应使等待以取消方式结束。

## 何时使用

- 需要“尽快触发一次然后每隔 N 时间触发”：用 `interval`，并在循环里 `co_await it.tick()`。
