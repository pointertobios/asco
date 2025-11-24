# Panic 模块（asco::panic/panic.h）

本文档介绍 ASCO 框架中的 panic 机制及其头文件 `panic/panic.h` 的接口与用法。

## 概述

Panic 用于处理**不可恢复的致命错误**，如断言失败、严重逻辑漏洞等。调用 panic 后，程序会立即终止，并可输出详细的错误信息与栈回溯，便于定位问题。

## 主要接口

### 1. `panic(std::string msg) noexcept`

- 触发 panic，输出指定消息，终止程序。
- `[[noreturn]]`：不会返回。
- 推荐用于普通同步场景。

### 2. `co_panic(std::string msg) noexcept`

- 协程环境下触发 panic，输出消息并终止。
- 用法与 `panic` 类似。

### 3. 格式化接口

- 支持 C++20 std::format 风格：

```cpp
panic("错误码: {}，原因: {}", code, reason);
co_panic("协程异常: {}", info);
```

- 自动格式化参数并输出。

### 4. `register_callback(std::function<void(cpptrace::stacktrace &, std::string_view)> cb)`

- 注册 panic 回调，可自定义 panic 行为（如日志落地、核心转储等）。
- 回调参数：
  - `cpptrace::stacktrace &`：当前栈（包括异步调用链，当使用 co_panic 时）回溯信息。
  - `std::string_view`：panic 消息。
- 适合集成自定义监控、调试工具。

## 使用示例

```cpp
#include <asco/panic/panic.h>

void fatal_error() {
    asco::panic::panic("致命错误，无法恢复");
}

void format_example(int code) {
    asco::panic::panic("错误码: {}", code);
}

// 注册自定义回调
asco::panic::register_callback([](cpptrace::stacktrace &st, std::string_view msg) {
    // 可在此保存日志、输出栈（异步调用链）信息等
});
```

## 设计要点

- panic 只用于**绝不应继续执行**的场景。
- 格式化接口便于输出结构化错误信息。
- 回调机制支持扩展和集成第三方工具。
- 协程专用接口保证异步场景下的正确终止。

## 与断言的关系

- `asco_assert` 断言失败时会调用 panic，统一致命错误处理路径。
- panic 可作为所有“不可恢复”错误的终极出口。
