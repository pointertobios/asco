# Select 选择器

`select` 用于“竞速”多个异步分支：同时启动（或等待）若干个可取消分支，**返回第一个完成的分支结果**，并触发其余分支尽快结束。

典型用途：

- 同时等待多个异步操作，谁先完成就取谁；
- 把“超时 / 取消信号 / 数据到达”等事件与主任务一起竞速；
- 用一个统一的 `std::variant` 承载“本次到底是哪条分支赢了”。

## 基本用法

`select` 必须在 worker 线程（也就是协程运行时）里构造。

```cpp
#include <asco/future.h>
#include <asco/select.h>

using namespace asco;

future<int> async_main() {
    auto sele = asco::select{}
        .along_with([](std::shared_ptr<context>) -> future<int> { co_return 42; })
        .along_with([](std::shared_ptr<context>) -> future_spawn<float> { co_return 3.14f; });

    auto v = co_await sele;

    std::visit(
        [](auto&& br) {
            using B = std::decay_t<decltype(br)>;
            if constexpr (B::branch_index == 0) {
                int x = *br;
                (void)x;
            } else if constexpr (B::branch_index == 1) {
                float y = *br;
                (void)y;
            }
        },
        v);

    co_return 0;
}
```

## 分支类型（along_with 重载）

`select` 支持两类分支：

### 1) cancellable_function 分支（会注入 select 内部 ctx）

形如：

```cpp
Fn(std::shared_ptr<context> ctx, Args...)
```

- `ctx` 由 `select` **内部创建并注入**；当某个分支胜出后，`select` 会取消这个内部 `ctx`，用于通知其它分支尽快退出。
- `Fn` 可以返回 `future<T>` 或 `future_spawn<T>`（以及其它满足框架约束的 future 类型）。

### 2) cancellable_waitable 分支（等待一个“外部对象”完成）

形如：

```cpp
sele.along_with(waitable);
```

其中 `waitable` 满足：

- `waitable->operator co_await()` 返回 `yield<notify*>`（即它本质是个“可被 notify 唤醒的等待点”）；
- `waitable->get_notify()` 返回 `notify&`；
- `deliver_type` 必须是 `void`。

最常见的例子是 `std::shared_ptr<context>`：`co_await ctx;` 会等待它被取消或被内部 `notify` 对象唤醒。

> 重要：把 `std::shared_ptr<context>` 作为 waitable 传给 `.along_with(ctx)` 时，它是“一个等待分支”。

## branch_index 规则（索引语义）

每次调用一次 `.along_with(...)`，都会在 select 中追加一个分支，并赋予该分支一个稳定的索引：

- 第一个追加的分支索引为 `0`
- 第二个为 `1`
- 以此类推

在返回值的 `std::variant` 中，可以通过 `T::branch_index` 判断是哪条分支胜出。

## 返回值类型

`co_await select` 的返回值是一个 `std::variant`：每个备选项都是 `branch<I, T>`。

- `I` 是分支索引（见上节）。
- `T` 是该分支的返回类型；若分支返回 `void`，则会被替换为 `std::monostate`。
- `branch` 提供 `operator*` / `operator->`，用于访问返回值。

直观理解：如果你按顺序追加了 `N` 个分支，它的返回类型等价于：

```cpp
future<std::variant<
    branch<0, monostate_if_void<T0>>,
    branch<1, monostate_if_void<T1>>,
    ...,
    branch<N-1, monostate_if_void<TN_1>>
>>
```

其中 `Tk` 是第 `k` 个分支的 deliver type。

## 取消与唤醒语义

当某个分支胜出后，`select` 会做两件事来让其它分支尽快结束：

1. 取消“内部 ctx”
   - 这只影响 cancellable_function 分支中收到的那个 `ctx`；
   - 你的分支可以 `co_await ctx;` 或检查 `ctx->is_cancelled()` 来退出。

2. 对所有 waitable 分支的 `notify` 调用 `notify_all()`
   - 这会把 waitable 从等待中唤醒，让它们有机会结束。

> 注意：对外部 waitable（例如你传入的 `std::shared_ptr<context>`）来说，`notify_all()` 只是“唤醒等待”，并不会调用 `cancel()` 改变其 cancelled 状态。

## 异常语义

- 如果**胜出的分支抛出异常**，`select` 会在取消/唤醒其它分支后，把异常重新抛出给 `co_await select` 的调用方。
