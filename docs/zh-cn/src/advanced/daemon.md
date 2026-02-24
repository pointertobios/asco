# `asco::core::daemon`：后台守护线程基类

`asco::core::daemon` 用于把一个“循环执行的后台工作”封装为一个对象。

- `daemon` 在内部启动一个后台线程。
- 线程按固定生命周期运行：`init()` → 重复调用 `run_once(...)` → `shutdown()`。
- 对象析构时会请求线程停止，并等待线程退出。

头文件：`asco/core/daemon.h`

---

## 1. 生命周期

### 1.1 启动：`start()`

`start()` 启动后台线程，并返回一个 `init_waiter`。

语义：

- 后台线程开始后会先调用 `init()`。
- `init_waiter` 析构时会等待 `init()` 完成。

典型用法（在派生类构造函数中启动，并等待初始化完成）：

```cpp
#include <asco/core/daemon.h>

struct my_daemon : asco::core::daemon {
    my_daemon() : daemon("my-daemon") {
        auto _ = start();
        // 这里开始可以认为 init() 已经完成
    }

    bool init() override;
    bool run_once(std::stop_token &st) override;
    void shutdown() override;
};
```

> `start()` 是受保护成员函数，只能在 `daemon` 的派生类内部调用。

### 1.2 停止：析构函数

当 `daemon` 对象析构时：

- 会请求后台线程停止（通过 `std::stop_token` 传递 stop 请求）。
- 会调用一次 `awake()`，用于唤醒正在等待的后台线程。
- 会等待后台线程退出。

---

## 2. 线程执行逻辑：`init()` / `run_once()` / `shutdown()`

### 2.1 `init()`

```cpp
virtual bool init();
```

语义：

- 在后台线程开始工作前调用一次。
- 返回 `true` 表示初始化成功；返回 `false` 表示启动失败。
- 当 `init()` 返回 `false` 时，后台线程不会进入 `run_once()` 循环，并会调用 `shutdown()`。

### 2.2 `run_once(std::stop_token &st)`

```cpp
virtual bool run_once(std::stop_token &st);
```

语义：

- 在后台线程中反复调用。
- 当 `st.stop_requested()` 为 `true` 时，调用方应尽快结束当前轮次并返回。
- 返回 `true` 表示继续下一轮；返回 `false` 表示结束循环。

### 2.3 `shutdown()`

```cpp
virtual void shutdown();
```

语义：

- 在线程退出前调用一次，用于资源释放与收尾。
- 无论 `init()` 失败还是正常退出循环，都会调用 `shutdown()`。

---

## 3. 唤醒与等待：`awake()` 与 `sleep_until_awake*`

### 3.1 `awake()`

```cpp
void awake();
```

语义：

- 使一次等待中的 `sleep_until_awake*` 结束等待。
- 可由任何线程调用。

### 3.2 等待：`sleep_until_awake...`

`daemon` 提供一组等待函数，供 `run_once()` 在“无事可做时”进入等待：

- `sleep_until_awake()`：等待直到被 `awake()` 唤醒。
- `sleep_until_awake_for(duration)`：等待直到被唤醒或超时。
- `sleep_until_awake_before(time_point)`：等待直到被唤醒或到达指定时间点。

语义：

- 这些函数在当前线程内等待。
- 发生唤醒或超时后返回。
- 这些函数不返回“唤醒原因”；如需区分原因，请在 `run_once()` 中自行检查条件。

---

## 4. 使用建议

- 在 `run_once()` 中优先使用 `sleep_until_awake*` 进入等待；这样析构时的停止请求能更快生效。
- `run_once()` 应避免无限阻塞：若必须等待外部事件，建议设置超时并周期性检查 `st.stop_requested()`。
- `awake()` 表示“有新工作/状态变化”；调用 `awake()` 前后如何更新共享状态由派生类自行约定。
