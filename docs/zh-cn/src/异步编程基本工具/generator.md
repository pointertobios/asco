# 异步生成器 `generator<T>`

`generator<T>` 是 asco 提供的“按需拉取”的异步序列工具。它让你在一个协程中逐个 `co_yield` 元素，消费端以异步方式一项项地拉取并处理，适合流式处理、分页读取、或逐步产生结果的场景。

核心类型与别名：

- `asco::generator<T>`：标准生成器
- `asco::generator_core<T>`：核心生成器

生成器的本体是一个协程函数：在生产端用 `co_yield` 逐项产出 `T`，完成时 `co_return;`；消费端用 `while (g)` 搭配 `co_await g()` 逐项拉取。

## 快速示例

```cpp
#include <asco/generator.h>
#include <asco/future.h>

using asco::generator;
using asco::future;
using asco::future_inline;

// 生产端：生成 1..n
generator<int> gen_count(int n) {
    for (int i = 1; i <= n; ++i) {
        co_yield i;
    }
    co_return; // 可省略
}

// 消费端：累加求和
future_inline<int> consume_sum(generator<int>& g) {
    int sum = 0;
    while (g) {                // g 仍可产出
        int v = co_await g();  // 拉取下一项
        sum += v;
    }
    co_return sum;
}

future<int> async_main() {
    auto g = gen_count(1000);
    auto sum = co_await consume_sum(g);
    // sum == 1000 * 1001 / 2
    co_return 0;
}
```

## API 与语义

- 定义：`template<class T> generator<T> f(...);`
  - 在函数体内可多次 `co_yield T` 产出元素
  - 函数正常结束或 `co_return;` 代表序列结束

- 消费：
  - `bool(g)`：判断生成器是否仍可继续产出（未关闭）
  - `co_await g()`：异步拉取下一项，返回 `T`；若生成器已结束，会抛出 `asco::runtime_error`（见下一节）

### 异常与结束

- 生产端抛出异常：
  - 消费端在消费完所有已经产生的值后再调用 `co_await g()` 时会重新抛出该异常；随后生成器关闭，`bool(g)` 返回 false

- 正常结束：
- IO 分块读取

```cpp
#include <asco/io/file.h>
#include <asco/io/buffer.h>

using asco::io::file;
using asco::io::buffer;

generator<buffer<>> read_chunks(file& f, size_t chunk_size) {
    while (true) {
        auto r = co_await f.read(chunk_size);
        if (!r.has_value())
            co_return; // EOF
        co_yield std::move(*r);
    }
}
```

建议的消费模式：

```cpp
while (g) {
    int v = co_await g();
    // 处理 v
}
```

如需区分“异常结束”与“正常结束”，可在循环外包一层 try/catch：

```cpp
try {
    while (g) {
        int v = co_await g();
        // 处理 v
    }
} catch (const std::exception& e) {
    // 处理异常路径
}
```

## 并发与调度

- `co_yield` 默认是“无背压”的快速产出点，不会在每次 `co_yield` 处挂起。
- 消费端通过一次 `co_await g()` 拉取一个元素。
- 若需要更强的背压（例如严格一产一消），可在上层协议中控制消费节奏，或引入限速逻辑。

## 典型用法

- IO 分块读取

```cpp
generator<buffer> read_pages(file& f, size_t page, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto buf = co_await f.read(page + i);
        assert(buf);
        co_yield std::move(*buf);
    }
}
```

## 与 `future<T>` 的关系

- `future<T>` 符合 `<asco/utils/concepts.h>` 中的概念 `async_function` ，但是生成器不符合。
- 生成器本身仍然是一个协程任务，遵循 asco 的调度模型。
- 生成器没有最终的 `T` 返回值，其“结果”是一串通过 `co_yield` 逐步产出的元素。

## 错误与最佳实践

- 在消费端使用 `while (g)` 做循环条件，避免最后一次 `co_await g()` 在生成器结束后抛错影响逻辑。
- 如果你确实需要显式检查结束，可用 try/catch 捕获异常并在 catch 中处理收尾。
- 生成器可移动，不可拷贝。移动后使用新对象继续消费，旧对象不再可用。
