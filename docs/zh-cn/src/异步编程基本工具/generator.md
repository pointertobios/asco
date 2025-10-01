# 异步生成器 `generator<T>`

`generator<T>` 是 ASCO 提供的“按需拉取 (pull-based)” 异步序列工具：

- 生产端：在协程中使用 `co_yield` 逐步产出元素。
- 消费端：通过 `co_await g()` 拉取一个 `std::optional<T>`；当为 `std::nullopt` 表示序列结束（正常 EOF）。

它适合以下场景：

1. 大文件 / 网络流按块增量处理（无需一次性加载）。
2. 组合/拼接多源数据流。
3. 惰性（延迟）计算序列。
4. 需要在“部分结果已产生”后仍可能抛出的管线（异常延迟重抛机制）。

核心类型：

- `asco::generator<T>`：标准生成器
- `asco::generator_core<T>`：核心生成器

生成器的本体是一个协程：在生产端用 `co_yield` 逐项产出 `T`，完成时 `co_return;`；消费端用 `while (g)` 搭配 `co_await g()` 逐项拉取。

## 快速示例

```cpp
#include <asco/generator.h>
#include <asco/future.h>
using asco::generator;
using asco::future_inline;

generator<int> gen_count(int n) {
    for (int i = 1; i <= n; ++i) co_yield i;
    co_return;
}

future_inline<int> consume_sum(generator<int> &g) {
    int sum = 0;
    while (auto opt = co_await g()) {
        sum += *opt;
    }
    co_return sum;
}
```

## API 与行为语义

生产端：

```cpp
generator<T> foo(...) {
    co_yield T{...};   // 多次
}
```

消费端：每次 `co_await g()`：

- 若有值：返回 `std::optional<T>`（含值）
- 若正常结束或数据已经全部取完：返回 `std::nullopt`
- 若生产端抛出异常：
  - 已成功产出的值仍可依次消费（按 `co_yield` 顺序）
  - 当所有已产出的值都被取走后，下一次 `co_await g()` 会重新抛出该异常

因此异常只在“清空残余缓存后”才会被重新抛出，保证不丢失已产生的数据。

### 异常传递时序

内部逻辑：

1. 生产端 `co_yield` 成功 -> 值进入无锁队列并通过信号量唤醒等待的消费端。
2. 生产端 `return_void()` 或异常路径都会调用 `yield_tx.stop()` 关闭通道。
3. 关闭后 `operator bool()` 即返回 false，但队列中可能还有尚未被消费的值。
4. 消费端调用 `g()` 时：
   - 首先等待信号量
   - 如果存在异常且尚未消费的产出数为 0，则抛出异常
   - 否则弹出一个值或得到空（结束）

## 生命周期与所有权

- 生成器对象可移动，不可拷贝。
- 当消费端读取到 `nullopt` 后，可安全销毁生成器对象；若仍持有其内部 future 状态不会泄露资源（框架回收）。

## 性能与内存特征

- 单次 `co_yield` 为常数时间（队列 push + 原子计数 + 信号量 release）。
- 大对象分块策略：将大数据拆分成独立 `T` 小块能更好地与消费者并发管线。
- 若生产端产生速度远快于消费端：由于当前实现为“无背压快速通道”，可能导致队列膨胀；此时：
  1. 在生产端插入自定义节流点；
  2. 或让 `T` 变为轻量句柄（引用计数 / span）而非完整数据实体。

## 与超时 / select / 中断协作

`generator<T>::operator()` 返回的是一个可 `co_await` 的 future，可与框架里的 `select` / 超时工具组合：

注意：生成器本身不感知外部超时或取消，它只是“取值 future”。若需要对“长时间不产出”做中止，可在外层 select 中放入 abort/flag 并提前丢弃剩余生成器。

## 典型使用模式

1. 文件/套接字分块读取 -> 解析器：
   - 生成器输出原始块；解析器再组包。
2. 多源合并：多个生成器并发供数据，外层调度 select 竞争获取。
3. 惰性计算：对一个巨大数学序列做前 N 项扫描，一旦判定提前结束条件满足就停止拉取。
4. 流式过滤 / map：封装一个接收上游生成器并 `co_yield` 转换后的新生成器。

## 组合示例：包装 map/filter

```cpp
template<class G, class F>
requires std::is_same_v<decltype(*std::declval<G&>()()), std::optional<typename G::value_type>>
auto map_gen(G upstream, F f) -> generator<decltype(f(std::declval<typename G::value_type&>()))> {
    while (auto v = co_await upstream()) {
        co_yield f(*v);
    }
}
```

## 背压与调度

- 每个 `co_yield` 使用无阻塞快速路径（`std::suspend_never`），推送后立即返回继续执行生成逻辑。
- 由消费端节奏决定整体推进速度；若需要显式背压，可在生产端主动 `co_await` 某些自定义节流 future。

## IO 分块读取示例（使用 optional 协议）

```cpp
generator<buffer<>> read_chunks(file &f, size_t chunk_size) {
    while (auto r = co_await f.read(chunk_size)) {
        co_yield std::move(*r);
    }
}

future_inline<void> consume(file &f) {
    auto g = read_chunks(f, 4096);
    size_t total = 0;
    while (auto b = co_await g()) {
        total += b->size();
    }
}
```

### 与 `future<T>` 的关系

- 生成器本身继承框架基础异步状态，但 `generator<T>` 的“结果”是一串离散的中间值。
- `generator<T>` 不是一个 `async_function`（无法直接 `co_await` 得到最终聚合值）。
- 可把生成器视为“异步单向通道”：`co_yield` 写入，`co_await g()` 拉取。

## 最佳实践

1. 一律使用 `while (auto v = co_await g())` 的迭代模式。
2. 需要区分正常结束 / 异常：

   ```cpp
   try {
       while (auto v = co_await g()) {
           // consume *v
       }
   } catch (const std::exception &e) {
       // 仅在生产端真异常时到这里
   }
   ```

3. 生成器可移动，不可拷贝；移动后旧对象失效。
4. 避免长时间在单次 `co_yield` 之间写大量 CPU 循环而不让出（必要时显式 `co_await asco::yield{}`）。
5. 若需要“热”消费（持续高频拉取），注意批量处理 / 聚合减少调度开销。
