# 项目约定

## 架构

- 公开 API 头文件位于 `asco/` 下；面向用户的异步抽象优先放在这里，运行时内部实现放在 `asco/core/` 下。
- `asco/core/` 下的改动应视为运行时语义改动。调度、取消、生命周期、并发和资源释放路径都需要更谨慎的推理与测试。
- `tests/`、`examples/` 和 `benchmarks/` 的 `CMakeLists.txt` 使用显式源码列表。新增 `.cpp` 文件时，通常也需要同步更新对应的 `CMakeLists.txt`。
- 不要修改 `build/` 下的生成文件。

## 构建与测试

- 参考 [asco-build skill](../.github/skills/asco-build/SKILL.md)。

## 约定

- 对于用户明确提出的长期协作要求，应记录到全局记忆中，避免后续机械地只遵循仓库文档而让用户重复提出相同要求。
- 遵循 [.clang-format](../.clang-format)，避免无关的格式化改动。
- 新增测试文件必须加入 [tests/CMakeLists.txt](../tests/CMakeLists.txt)。测试使用 `ASCO_TEST(...)`、`ASCO_CHECK(...)` 和 `asco::test`；参见 [docs/zh-cn/src/advanced/testing.md](../docs/zh-cn/src/advanced/testing.md)。
- [docs/zh-cn/src](../docs/zh-cn/src) 下的中文文档是实际维护的 API 文档。新增文档页时，需要同时更新 [docs/zh-cn/src/SUMMARY.md](../docs/zh-cn/src/SUMMARY.md) 和对应章节的 `README.md`。
- 除非 API 会显式消费 guard 作为操作的一部分，否则不要让自旋锁 guard 跨越 `co_await` 存活。
- 尤其注意把引用传给协程入口或 awaitable 的代码，除非能显而易见地证明引用不会悬空和 UAF，否则应该高度怀疑此处引用的传递是否安全。

## 评审要求

- 始终警告用户不要盲目相信你给出的修改是正确的，尤其是涉及 `asco/core`、并发、取消、生命周期或资源释放路径时，必须由用户自行核对语义、边界条件和失败路径。
- 对于非琐碎改动，在 PR 中明确说明问题背景、行为变化、已检查路径和未完全覆盖的风险。
- 对于 `asco/core` 或其他运行时语义改动，需要能够不依赖 AI 摘要，独立说明不变量、失败路径、资源释放以及并发或取消风险。
- 完整贡献要求见 [CONTRIBUTING.md](../CONTRIBUTING.md) 和 [docs/zh-cn/src/advanced/contribute/anti-vibe.md](../docs/zh-cn/src/advanced/contribute/anti-vibe.md)。

## 关键参考

- 从 [README.md](../README.md) 开始了解项目的对外概览。
- 以 [examples/common.cpp](../examples/common.cpp) 作为最小入口示例。
- 以 [tests/cancellation.cpp](../tests/cancellation.cpp) 和 [tests/sync/channel.cpp](../tests/sync/channel.cpp) 作为代表性的异步测试模式参考。
