# 测试框架

ASCO 提供面向协程与异步代码的轻量级测试框架。测试目标链接 `asco::test` 后即可使用；如需运行时、任务调度、同步原语或时间组件，通常还应同时链接 `asco::core`。

当前实现特性：

- 测试用例为协程，返回 `future<asco::test::test_result>`。
- 单个测试可执行文件可注册多个用例。
- 测试主程序创建带 timer 的多线程 runtime，并通过 `join_set` 并发执行全部已注册用例。
- 输出顺序取决于用例完成顺序。
- 框架统计通过、失败、忽略数量；存在失败时进程返回非 0。
- `asco::test` 提供测试 `main()`，并自动定义 `ASCO_TESTING`。

## 核心接口

公开接口位于 `asco/test/test.h`：

- `ASCO_TEST(name, ...)`：声明并注册测试用例。
- `ASCO_CHECK(expr, fmt, ...)`：断言失败时立即结束当前测试。
- `ASCO_CHECK_WITH_FAILCALLBACK(callback, expr, fmt, ...)`：断言失败前执行清理或取消逻辑。
- `ASCO_SUCCESS()`：返回成功。
- `ASCO_IGNORE_TEST`：将测试结果标记为忽略。

## 编写测试用例

### 基本结构

```cpp
#include <asco/test/test.h>

using namespace asco;

ASCO_TEST(my_first_test) {
    ASCO_CHECK(1 + 1 == 2, "math broken: {}", 1 + 1);
    ASCO_SUCCESS();
}
```

- `name` 作为测试名输出。
- `ASCO_TEST` 在静态初始化阶段完成注册。
- 测试函数通常以 `ASCO_SUCCESS()` 结束。

### ASCO_CHECK

`ASCO_CHECK(expr, fmt, ...)` 在 `expr` 为假时立即返回失败结果。错误信息包含：

- `std::format(fmt, ...)` 生成的消息。
- 当前源码位置，包括文件名、行号、列号。

示例：

```cpp
ASCO_CHECK(size == expected, "size mismatch: got={}, expected={}", size, expected);
```

### ASCO_CHECK_WITH_FAILCALLBACK

`ASCO_CHECK_WITH_FAILCALLBACK(callback, expr, fmt, ...)` 与 `ASCO_CHECK` 等价，但在断言失败前先执行 `callback()`。适用于超时、后台任务或需要回收资源的场景。

```cpp
auto h = spawn([&]() -> future<void> {
    co_await time::sleep_for(5s);
});

ASCO_CHECK_WITH_FAILCALLBACK(
    [&]() { h.cancel(); },
    co_await wait_until([&]() { return h.await_ready(); }, 4096),
    "sleep task did not finish in time");
```

### ASCO_IGNORE_TEST

```cpp
ASCO_TEST(flake_case, ASCO_IGNORE_TEST) {
    ASCO_SUCCESS();
}
```

一项测试标记了 `ASCO_IGNORE_TEST` 后统计结果中记为“忽略”，不参与通过/失败统计。

## panic 与异常处理

在测试环境下，`asco::panic(...)` 会抛出 `asco::panicked`，可用于验证错误路径：

```cpp
#include <asco/panic.h>

ASCO_TEST(expect_panic) {
    bool caught = false;
    try {
        asco::panic("boom");
    } catch (asco::panicked &e) {
        (void)e;
        caught = true;
    }

    ASCO_CHECK(caught, "panic should throw asco::panicked under ASCO_TESTING");
    ASCO_SUCCESS();
}
```

测试主程序的异常处理策略：

- 捕获 `asco::panicked` 并记为失败，输出 `panic: ...`。
- 捕获其他异常并记为失败，输出“发生异常”。

应按 `asco::panicked &` 捕获，不应依赖 `std::exception &`。

## 异步测试模式

测试用例本身是协程，可直接：

- `co_await` 任意 ASCO future 或 awaitable。
- 通过 `spawn(...)` 启动并发任务。
- 使用 `co_await this_task::yield()` 主动让出执行权。
- 等待 `join_handle` 完成。

当前异步工具并不完善，建议为异步条件提供有界等待，避免测试永久挂起：

```cpp
#include <functional>
#include <asco/this_task.h>

template<class Pred>
asco::future<bool> wait_until(Pred &&pred, std::size_t max_spins = 4096) {
    for (std::size_t i = 0; i < max_spins; ++i) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await asco::this_task::yield();
    }
    co_return std::invoke(pred);
}
```

```cpp
ASCO_CHECK(co_await wait_until([&] { return ready.load(); }), "condition did not become true");
```

实践建议：

- 优先使用 `yield` 配合条件轮询，避免固定睡眠。
- 后台任务应设置边界，并在失败路径执行取消或清理。
- 用例并发执行时，不应共享未同步的全局可变状态。

## CMake 与 CTest 接入

### 最小接入方式

```cmake
add_executable(test_channel channel.cpp)
target_link_libraries(test_channel PRIVATE asco::core asco::test)

add_test(NAME test_channel COMMAND test_channel)
```

- 无需自定义 `main()`。
- 无需手工定义 `ASCO_TESTING`。

### 当前仓库的组织方式

当前仓库将多个测试源文件聚合为单一测试目标：

```cmake
add_executable(tests
    cancellation.cpp
    hash_map.cpp
    sync/mutex.cpp
    sync/semaphore.cpp
    task_local.cpp
    time.cpp
)
target_link_libraries(tests PRIVATE asco::core asco::test)

add_test(tests tests)
```

- CTest 层面当前仅注册一个测试项 `tests`。
- 具体用例由该可执行文件内部统一注册并运行。

## 运行测试

通过 CTest：

```bash
ctest --test-dir build --output-on-failure
```

按名称过滤当前仓库的测试目标：

```bash
ctest --test-dir build -R '^tests$' --output-on-failure
```

直接运行测试可执行文件：

```bash
./build/tests/tests
```

典型输出：

```text
[通过] semaphore_basic_try_acquire_release
[失败] sleep_for_waits_at_least_duration: ...
[忽略] hash_map_concurrent_stress
测试结果：10 通过，1 失败，2 忽略
```

## 基准测试辅助工具

`asco/test/bench.h` 提供 `asco::test::bench_context`，当前用于 `benchmarks/channel.cpp`。

```cpp
asco::test::bench_context bench{"channel_e2e_latency", warmup, measure};

auto head = bench.get_span();
if (bench.commit(head)) {
    // 达到 warmup + measure 次提交
}
```

- `warmup` 次提交仅用于预热。
- 随后的 `measure` 次提交参与统计。
- 析构时输出 `avg`、`max`、`p50`、`p90`、`p99`、`p999`。

该工具不参与 `ASCO_TEST(...)` 的通过/失败判定，也不接入 CTest。
