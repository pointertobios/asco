# Future 类型系统

ASCO 提供了三种核心的 Future 类型，用于不同的协程执行模式：`future<T>`、`future_spawn<T>` 和 `future_core<T>`。本文档将详细介绍这些类型的用法、生命周期与最佳实践。

## 类型概览

### `future<T>`

最基础的协程返回类型，用于同步执行模式。

```cpp
template<concepts::move_secure T>
using future = base::future_base<T, false, false>;
```

- 特点
  - 同步执行：协程在被 `co_await` 时继承调用者的工作线程
  - 直接控制流：不会自动调度到其他线程
  - 适用于需要线程亲和性的场景

### `future_spawn<T>`

用于异步执行的协程返回类型。

```cpp
template<concepts::move_secure T>
using future_spawn = base::future_base<T, true, false>;
```

- 特点
  - 异步执行：协程会被调度到工作线程池中执行
  - 自动负载均衡：runtime 会选择合适的工作线程
  - 支持同步等待（`.await()`）和异步等待（`co_await`）

### `future_core<T>`

核心任务的协程返回类型，用于运行时关键任务。当前行为与 `future_spawn<T>` 相同，但在未来的任务窃取（work stealing）机制中将获得特殊处理。

```cpp
template<concepts::move_secure T>
using future_core = base::future_base<T, true, true>;
```

- 特点
  - 异步执行：与 `future_spawn<T>` 类似，在工作线程池中执行
  - 核心标记：为即将引入的任务窃取机制预留
  - 规划特性：未来将不可被其他工作线程窃取，保证在固定线程执行

## 通用功能

所有 Future 类型都支持以下基本特性：

### 移动语义

- Future 只支持移动，禁止拷贝
- 确保任务的所有权清晰，避免资源泄漏

```cpp
future<int> f1 = foo();     // OK：移动构造
auto f2 = std::move(f1);    // OK：移动赋值
future<int> f3 = f1;        // 错误：禁止拷贝
```

### 异常传播

- 自动捕获并传播协程中的异常
- 在 `co_await` 或 `.await()` 时重新抛出

```cpp
future<void> may_throw() {
    if (error_condition)
        throw my_error{};
    co_return;
}

try {
    co_await may_throw();  // 异常会在这里被重新抛出
} catch (const my_error& e) {
    // 处理异常
}
```

### co_invoke

- 用于延长协程本身的生命周期并启动协程（通常用于 lambda 表达式）

```cpp
auto task = co_invoke([] -> future_spawn<void> {
    // 耗时任务
    co_return;
});
co_await task;  // lambda 表达式随协程一起销毁，此处安全
```

- 注意：**协程本身的销毁有额外的开销**

### 值类型约束

- 要求 T 满足 `move_secure` 概念（可移动）
- 支持 void 类型（`future<void>`）

## 执行模型

### 同步执行（`future<T>`）

```cpp
future<int> compute() {
    co_return 42;
}

// 在调用者线程中同步执行
future<void> caller() {
    int value = co_await compute();  // 不会发生异步调度
}
```

### 异步执行（`future_spawn<T>`）

```cpp
future_spawn<int> async_compute() {
    co_return 42;
}

// 两种等待方式
future_spawn<void> async_caller() {
    // 1. 异步等待（推荐）
    int value = co_await async_compute();
    
    // 2. 同步等待（仅在非工作线程中使用）
    // int value = async_compute().await();
}
```

## 进阶功能

### 任务转换

`future<T>` 可以转换为 `future_spawn<T>` 或 `future_core<T>`：

```cpp
future<int> normal() {
    co_return 42;
}

future_spawn<int> to_spawn(future<int> f) {
    return f.spawn();  // 转换为异步任务
}

future_core<int> to_core(future<int> f) {
    return f.spawn_core();  // 转换为核心任务
}
```

### 异常处理与忽略

`ignore()` 用于忽略对应 `future` 对象所代表的协程的返回值，并吞掉其抛出的异常（可选提供回调用于观测）。

- 返回值：`ignore()` 返回一个 `future<void>`（spawn 模式），当底层协程完成时该 future 也完成。
- 行为：调用 `ignore()` 会丢弃原协程的返回值；若协程抛出异常，默认不向上抛出；若提供了回调，会在捕获到异常时调用该回调并传入 `std::exception_ptr`。

常见用法：在后台启动任务但不关心返回值与错误，或者只想在异常发生时记录/观测但不传播它们。

```cpp
// 最常见：fire-and-forget（不等待，不传播异常）
background_task().ignore();

// 提供回调以记录异常（回调接受 std::exception_ptr）
cleanup_task().ignore([](std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (const std::exception &ex) {
        std::cerr << "Cleanup failed: " << ex.what() << '\n';
    }
});

// 如果需要等待完成但仍丢弃返回值与异常，可以 co_await
co_await some_future().ignore();
```

## 最佳实践

### 选择合适的 Future 类型

1. 默认使用 `future<T>`
   - 适用于大多数异步操作
   - 需要低异步调度开销
   - 无并发

2. 使用 `future_spawn<T>` 当：
   - 需要并发
   - 自动负载均衡
   - 良好的并发性能

3. 使用 `future_core<T>` 当：
   - 自动负载均衡
   - 需要线程亲和、保持不可窃取

### 异步编程指南

- 注意 lambda 表达式的生命周期

```cpp
// 错误示例：lambda 生命周期短于异步任务生命周期

auto task1 = []() -> future<void> {
    // 任务逻辑
    co_return;
}();
co_await task1; // 任务启动，但是前面的 lambda 表达式已经销毁，产生 use-after-free

auto task2 = []() -> future_spawn<void> {
    // 耗时任务逻辑
    co_return;
}();
co_await task2; // 任务在刚启动时行为正常，但是很快 lambda 表达式就将被销毁，产生 use-after-free

// 正确示例：使用 co_invoke 延长 lambda 表达式的生命周期

auto task = co_invoke([]() -> future<void> {
    // 任务逻辑
    co_return;
});
co_await task; // 任务安全执行
```

- 避免在工作线程中使用 `.await()`

```cpp
// 错误：在工作线程中同步等待
void wrong() {
    auto value = async_task().await();  // 会 panic
}

// 正确：使用 co_await
future_spawn<void> correct() {
    auto value = co_await async_task();
}
```

- 正确处理异常

```cpp
future_spawn<void> robust() {
    try {
        co_await risky_operation();
    } catch (const std::exception& e) {
        // 处理异常
        co_return;
    }
    // 继续执行
}
```

- 合理使用 `ignore()`

```cpp
// 适用于确实可以忽略返回值和异常的场景
background_task().ignore();

// 需要记录异常时提供回调
cleanup_task().ignore([](auto e) {
    log_error("Cleanup failed", e);
});
```

### 性能考虑

1. 避免不必要的任务转换
   - `spawn()` 和 `spawn_core()` 会创建新的协程对象
   - 如果最终要异步执行，直接返回对应类型

2. 合理使用同步/异步模式
   - 短小操作使用 `future<T>` 避免调度开销
   - IO 密集型操作使用 `future_spawn<T>` 提高并发度

## 调试技巧

### 异常追踪

- ASCO 会自动记录协程创建和异常发生的调用栈
- 异常会保留原始抛出点的上下文

### 任务状态检查

- 可通过 task_id 在运行时中查找任务
- 支持检查任务是否已完成、是否发生异常

## 注意事项

- 使用 `ignore()` 时要谨慎，确保返回值和异常可以被安全忽略
