# 带有 stacktrace 的动态断言 (`asco_assert`)

本文档介绍 ASCO 的断言宏 `asco_assert` 以及其底层实现、使用方式与最佳实践。

## 概述

`asco_assert(expr, [hint])` 用于在运行期验证**永远应该为真**的条件。若条件失败，它会调用内部的 `asco::assert_failed`，并最终触发 `panic::panic`，以不可恢复的方式终止程序（`[[noreturn]]`）。

与普通错误处理不同，断言面向**开发阶段的逻辑不变量**：

- 发现程序员假设被破坏的最早时机。
- 在失败点提供清晰的表达式字符串和可选提示信息。
- 将控制权交给 panic 框架，统一异常终止路径（着色输出、栈展开/回溯等）。

## 接口说明

### 宏：`asco_assert`

```cpp
#define asco_assert(expr, ...) \
    do {                       \
        if (!(expr)) {         \
            asco::assert_failed(#expr, ##__VA_ARGS__); \
        }                      \
    } while (0)
```

要点：

1. `expr` 只会被求值一次（包在 `if (!(expr))` 中），避免副作用重复执行。
2. 失败时字符串化表达式：`#expr`，便于定位逻辑。
3. 可选参数（提示 `hint`）通过 GNU 扩展 `##__VA_ARGS__` 消除空参数的逗号。
4. 展开后调用对应该签名的 `asco::assert_failed`。

### 函数：`asco::assert_failed`

```cpp
[[noreturn]] void assert_failed(std::string_view expr);
[[noreturn]] void assert_failed(std::string_view expr, std::string_view hint);
```

实现（`assert.cpp`）内部直接委托给：

```cpp
panic::co_panic("Assertion failed on {}", expr);
panic::co_panic("Assertion failed on {}: {}", expr, hint);
```

因此：

- 所有断言失败统一走 panic 终止路径；
- 输出格式固定，便于日志检索；
- 由于采用 `std::string_view`，传入的 `hint` 应为字符串字面量或生命周期足够长的缓冲（避免悬垂）。

## 使用示例

### 基本用法

```cpp
int idx = compute_index();
asco_assert(idx >= 0, "索引必须非负");
```

失败输出示例：

```text
Assertion failed on idx >= 0: 索引必须非负
```

### 无提示信息

```cpp
asco_assert(ptr != nullptr);
```

输出：

```text
Assertion failed on ptr != nullptr
```

### 结合内部不变量

```cpp
struct range { int l; int r; };
void normalize(range& rg) {
    asco_assert(rg.l <= rg.r, "range 左值不能大于右值");
    // ... normalize logic
}
```

### 避免副作用重复

虽然表达式只执行一次，仍建议保持无副作用：

```cpp
// 不推荐：含副作用的断言表达式会降低可读性
asco_assert(vec.pop_back() > 0, "不应为空");
```

## 与 Panic 框架的集成

断言失败直接调用 `panic::co_panic`：

- 继承 panic 框架的彩色/结构化输出（若已实现）。
- 可以统一在 co_panic 处理路径中做栈回溯、日志落地、核心转储等。
- 所有断言失败为“致命”级别，不返回调用者。

这使断言只负责“检测 + 定位”，而终止策略（如是否生成 dump）完全由 panic 系统集中配置，降低分散的错误处理复杂度。

## 性能与开销

当前实现**无条件启用**：

- 每个断言在成功时的开销 ≈ 一次条件分支 + 未触发时的宏展开（极低）。
- 失败路径较重（格式化 + panic 逻辑）。

在性能关键路径依旧可以使用断言，但需确保：

1. 断言表达式本身是 O(1) 且无昂贵副作用；
2. 不使用复杂的临时格式化（`hint` 仅接受一个 `std::string_view`，避免构造代价）。

## 最佳实践

1. 只断言“绝不该失败”的内部不变量；可预期失败的输入应做显式校验与返回错误。
2. 保持表达式短小、可读：逻辑复杂时先拆成局部变量再断言。
3. `hint` 写明为何“不变量应成立”，而非简单重复表达式。例：`"range 必须标准化后 l <= r"`。
4. 不在断言中做资源释放等副作用动作，断言失败后不会继续执行。

5. 若需要更丰富提示（多变量格式化），建议在调用前自行构造稳定的字符串缓冲再传入（目前接口只接受一个 `std::string_view`）。

## 与标准库 / 其他框架对比

| 项目               | 行为                              | 自定义提示 | 终止统一性 |
| ------------------ | --------------------------------- | ---------- | ---------- |
| `assert(expr)` (C) | 条件失败 `abort()`                | 需修改源码 | 分散       |
| `std::assert` (宏) | 同上                              | 需修改源码 | 分散       |
| `asco_assert`      | 进入 panic 流程（可扩展统一终止） | 有         | 统一       |

## 常见误用与规避

| 误用                         | 风险说明       | 推荐做法                   |
| ---------------------------- | -------------- | -------------------------- |
| 在表达式中包含副作用         | 语义不清       | 将副作用前置，断言纯条件   |
| 用断言替代输入合法性校验     | 用户可触发崩溃 | 使用返回值/错误码/异常     |
| 传入临时构造的短生命周期缓冲 | 悬垂引用       | 使用字符串字面量或静态缓存 |
