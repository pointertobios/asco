# `asco::core::daemon`（守护线程基类）

本文档说明如何继承和使用 `asco::core::daemon` 类型来实现后台守护线程。

## 概述

`asco::core::daemon` 是一个轻量的守护线程基类，封装了线程生命周期、初始化同步、以及唤醒/睡眠的常用逻辑。典型用法是继承该类并覆盖 `init()`、`run_once(std::stop_token &)` 和 `shutdown()` 三个虚方法来实现特定行为。

核心特性：

- 使用 `std::jthread` 管理后台线程，支持基于 `std::stop_token` 的干净停止请求。
- 提供 `awake()` / `sleep_until_awake*()` 系列方法用于线程间唤醒与有时限的等待。
- 构造/启动/初始化通过 `start()` + 返回的 RAII `init_waiter` 实现线程初始化同步，确保启动方能等待后台线程完成 `init()`。
- 析构函数会请求停止并等待后台线程安全退出。

## 公有 API（概要）

- `daemon(std::string name)`：构造器，`name` 在 Linux 上用于线程命名（可读性和调试）。
- `~daemon()`：析构函数，会请求线程停止、唤醒线程（以解除阻塞），并 `join()` 线程。
- `void awake()`：释放内部信号量，唤醒调用 `sleep_until_awake()` 或其变体而阻塞的守护线程。

受保护的辅助接口（给子类使用）：

- `init_waiter start()`：启动后台线程并返回一个 `init_waiter`。`init_waiter` 的析构函数会等待后台线程释放 `init_sem`，因此可通过 RAII 在启动代码中等待初始化完成。
- `sleep_until_awake()` / `sleep_until_awake_for(...)` / `sleep_until_awake_before(...)`：基于内部 `std::binary_semaphore` 的阻塞/有时限等待，用于在 `run_once` 中等待事件或超时。

虚方法（子类通常需要覆盖）

- `virtual bool init()`：线程刚启动时调用一次。默认返回 `true`。返回 `false` 表示初始化失败，线程将调用 `shutdown()` 并退出。
- `virtual bool run_once(std::stop_token &st)`：守护主循环中每次执行的工作。返回 `true` 表示继续循环，返回 `false` 或在 `stop_requested()` 为真时退出循环。默认实现会 `sleep_until_awake()` 并返回 `true`。
- `virtual void shutdown()`：线程退出前的清理逻辑。

## 生命周期与典型启动模式

1. 在子类构造函数或初始化函数中调用 `start()`（受保护，因此通常在子类内部调用），它会创建后台 `jthread` 并立即返回一个 `init_waiter`。
2. 当 `init_waiter` 离开作用域时，其析构函数会阻塞直到后台线程完成 `init()` 并释放内部信号量，这样启动方就可以等待初始化完成。
3. 后台线程在循环中调用 `run_once(st)`，直到 `stop_requested()` 被置位或 `run_once` 返回 `false`。
4. 析构函数会发起 `request_stop()`，并调用 `awake()` 以解除阻塞，然后 `join()` 线程，保证线程安全退出。

示例（最小子类实现）：

```cpp
class my_daemon : public asco::core::daemon {
public:
    my_daemon() : daemon("my_daemon") {
        auto waiter = start(); // 启动线程并在 waiter 析构时等待 init 完成
    }

protected:
    bool init() override {
        // 初始化资源
        return true; // 返回 false 可终止线程
    }

    bool run_once(std::stop_token &st) override {
        // 等待唤醒或定时任务
        sleep_until_awake_for(std::chrono::milliseconds(500));

        if (st.stop_requested()) return false;

        // 执行一次工作
        do_work();
        return true; // 继续循环
    }

    void shutdown() override {
        // 清理资源
    }
};
```

在外部触发工作或唤醒守护线程：

```cpp
my_daemon d;
// 触发守护线程做一次立即处理
d.awake();
```

## 设计细节与注意事项

- `start()` 是 `protected` 的：意在由子类控制何时启动线程（例如在子类构造流程中）。
- `init_waiter` 的析构会 `acquire` 内部信号量，确保调用方等待完成；不要将其返回到会较晚析构的上下文，否则可能阻塞过久。
- `run_once` 接受 `std::stop_token &`：在实现中应检查 `st.stop_requested()`，并在请求停止时尽快返回 `false`。
- 析构过程中 `daemon` 会调用 `awake()` 以确保如果线程正阻塞在 `sleep_until_awake()` 上，则能被唤醒并退出。
- Linux 平台会把后台线程命名为构造时提供的 `name`（通过 `pthread_setname_np`），便于调试与崩溃分析。

## 推荐实践

1. 将耗时或阻塞的 I/O 放在 `run_once` 中，并确保响应 `stop_requested()`。
2. 避免在 `init()` 中执行可能长时间阻塞的操作（或在 `init()` 内使用超时机制），因为调用 `start()` 的上下文会等待 `init_waiter` 完成。
3. `awake()` 语义是“通知有新工作”，而不是强制中断正在执行的工作；若需要立即中断复杂任务，结合 `stop_token` 使用。
4. 若需要周期性工作，结合 `sleep_until_awake_for()` 或 `sleep_until_awake_before()` 实现合理的等待策略。

## 常见问题

- Q: 我可以在 `run_once` 中抛异常吗？
  - A: 当前基类未显式捕获异常，抛出异常会导致线程异常终止。建议在 `run_once` 中自行捕获并在 `shutdown()` 中做清理，或通过 `panic` 报告致命错误。

- Q: `start()` 返回的 `init_waiter` 需要手动保存吗？
  - A: 不需要长时间保存。通常以局部变量持有，确保在期望等待初始化完成的作用域结束时析构即可。
