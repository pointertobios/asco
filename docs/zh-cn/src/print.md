# 打印工具（print 库）

asco 提供了基于 C++23 `std::format` 的简洁打印 API，封装在头文件 `asco/print.h` 中，直接写入异步标准输出/错误流。

- 头文件：`#include <asco/print.h>`
- 适用场景：在异步任务中进行格式化输出。

## API 概览

- `print(fmt, args...)`：写到标准输出（不自动换行）。
- `println(fmt, args...)`：写到标准输出并追加 `\n`。
- `eprint(fmt, args...)`：写到标准错误（不自动换行）。
- `eprintln(fmt, args...)`：写到标准错误并追加 `\n`。

签名（简化）：

```cpp
future<void> print(std::format_string<Args...> fmt, Args&&... args);
future<void> println(std::format_string<Args...> fmt, Args&&... args);
future<void> eprint(std::format_string<Args...> fmt, Args&&... args);
future<void> eprintln(std::format_string<Args...> fmt, Args&&... args);
```

## 使用示例

```cpp
#include <asco/future.h>
#include <asco/print.h>

using asco::future, asco::println, asco::print, asco::eprintln;

future<int> async_main() {
    println("Hello, {}!", "ASCO");
    int answer = 42;
    print("answer = {}", answer);
    eprintln(" oops: {}", "something went wrong");
    co_return 0;
}
```

## 与标准库 `std::print` 的区别

- `std::print`/`std::println` 是同步阻塞的；asco 的 `print`/`println` 是异步的。
- asco 的实现直接集成到运行时的标准 IO 设施，便于在统一的异步模型中使用与测试。
