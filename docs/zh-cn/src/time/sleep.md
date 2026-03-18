# 睡眠（sleep）：`sleep_until` 与 `sleep_for`

本页介绍 `asco::time` 里的睡眠等待工具，用于在协程中进行延时。

## 时间基准

- 计时基于 `std::chrono::steady_clock`（单调递增，不受系统时间校准影响）。
- 等待接口语义上以“目标时间点是否已到”为准：如果目标已过去，应当立即完成而不是等待。

## `sleep_until`：睡到某个时间点

签名：`future<void> sleep_until(std::chrono::steady_clock::time_point tp)`

语义：

- 等待直到 `tp` 到达。
- 若 `tp` 在调用时已经是过去时间点，应立即返回。
- 可被取消：当调用方协程在等待期间收到取消请求时，等待会以取消方式结束（通常表现为对任务/句柄 `co_await` 时抛出 `core::coroutine_cancelled`）。

## `sleep_for`：睡一段时间

签名：`future<void> sleep_for(duration)`（`duration` 满足 `util::types::duration_type`）

语义：

- 等待至少给定的 `duration`。
- 若 `duration <= 0`，应立即返回。
- 与 `sleep_until` 一样可被取消。

## 何时使用

- 需要“延迟到某个绝对时间点”：用 `sleep_until`。
- 需要“延迟一段相对时间”：用 `sleep_for`。
