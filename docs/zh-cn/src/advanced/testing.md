# 测试框架

ASCO 自带一个**轻量级测试框架**，用于为协程/异步代码编写测试用例。

它并不只用于本项目自身：只要你的程序/库是基于 ASCO 构建的（能链接到 `asco::test`，通常还需要 `asco::core`），就可以直接复用这套测试框架来编写与运行测试。

它的核心特点是：

- 测试用例本身是 `future<test_result>`，可以在测试里直接 `co_await`。
- 同一个测试可执行文件中可注册多个用例，运行时会执行全部用例并汇总结果。
- 与 CMake/CTest 集成：每个测试目标是一个可执行文件，通过 `add_test()` 接入。

使用方式：

- 用例用 `ASCO_TEST(name)` 声明。
- 断言用 `ASCO_CHECK(...)`。
- 用例成功结束用 `ASCO_SUCCESS()`。
- 测试可执行文件需要链接 `asco::test`。

## 编写测试用例

### 1) 基本结构

每个用例通过 `ASCO_TEST(name)` 声明，其函数体是一个协程（返回 `future<test_result>`）：

```cpp
#include <asco/test/test.h>

using namespace asco;

ASCO_TEST(my_first_test) {
    ASCO_CHECK(1 + 1 == 2, "math broken: {}", 1 + 1);
    ASCO_SUCCESS();
}
```

- `name` 会作为测试名显示在输出中（例如 `my_first_test`）。
- 每个测试**必须**以 `ASCO_SUCCESS()` 结束（或在失败处提前 `co_return std::unexpected{...}`）。

### 2) 断言：`ASCO_CHECK`

`ASCO_CHECK(expr, fmt, ...)` 用于断言：

- 当 `expr` 为假时，测试立即失败并 `co_return std::unexpected{...}`
- 失败信息包含：
  - 你提供的格式化消息（`std::format(fmt, ...)`）
    - 当前源码位置（文件名/行/列）

建议：

- 让 `fmt` 清晰描述期望条件与实际情况
- 对于会等待的异步条件，配合 `co_await this_task::yield()` 自旋等待（见下文示例）

补充：

- 在测试框架下，`asco::panic(...)` 会抛出 `asco::panicked`，从而允许你在测试中捕获它，用于验证“应该 panic 的错误行为”。
- `asco::panicked` 不能通过 `std::exception &` 捕获，请按 `asco::panicked &` 捕获。

示例：

```cpp
#include <asco/panic.h>

ASCO_TEST(expect_panic) {
    bool caught = false;
    try {
        asco::panic("boom");
    } catch (asco::panicked &e) {
        (void)e; // 如需信息可用 e.to_string()
        caught = true;
    }

    ASCO_CHECK(caught, "panic should throw asco::panicked under ASCO_TESTING");
    ASCO_SUCCESS();
}
```

### 3) 异步/并发测试写法

测试是协程，因此可以自然地写异步逻辑，例如：

- `co_await sem.acquire()` / `co_await join_handle`
- `spawn([&] -> future<void> { ... })` 启动并发任务
- `co_await this_task::yield()` 让出执行权

一个常用的小工具模式是“等待条件成立”：

```cpp
#include <functional>
#include <asco/this_task.h>

template<class Pred>
asco::future<bool> wait_until(Pred &&pred, std::size_t max_spins = 4096) {
    for (std::size_t i = 0; i < max_spins; i++) {
        if (std::invoke(pred)) co_return true;
        co_await asco::this_task::yield();
    }
    co_return false;
}
```

再用 `ASCO_CHECK(co_await wait_until(...), "...")` 来避免无限等待。

## 新增一个测试目标（接入 CTest）

新增一个测试目标的最小步骤：

1) 新建一个测试源文件（例如 `tests/channel.cpp`），写入若干 `ASCO_TEST(...)` 用例。
2) 在 `tests/CMakeLists.txt` 中添加可执行文件，并用 CTest 注册：

```cmake
add_executable(test_channel channel.cpp)
target_link_libraries(test_channel PRIVATE asco::core asco::test)

add_test(channel test_channel)
```

说明：

- 链接 `asco::test` 后，不需要再为测试可执行文件提供 `main()`。
- 如果测试用到了 runtime/同步原语等功能，需要同时链接 `asco::core`。

## 运行测试

### 方式 A：使用 CTest

在已生成构建目录（例如 `build/`）的情况下：

- 运行全部测试：

```bash
ctest --test-dir build --output-on-failure
```

- 只跑某一个测试（按 CTest 名称过滤，例如 `semaphore`）：

```bash
ctest --test-dir build -R semaphore --output-on-failure
```

### 方式 B：直接运行测试可执行文件

你也可以直接运行构建产物，例如：

```bash
./build/tests/test_semaphore
```

当某个用例失败时，程序会以非 0 退出码结束，便于 CI/脚本判定。

## 命名与建议

- 测试用例名：建议使用 `模块_场景_期望` 的风格（例如 `semaphore_acquire_blocks_and_release_wakes`）。
- 一个测试文件可以包含多个 `ASCO_TEST`；按功能聚合（例如 `semaphore.cpp` 放信号量相关）。
- 尽量避免依赖睡眠/真实时间；优先用 `yield` + 条件等待来保证测试稳定性。
