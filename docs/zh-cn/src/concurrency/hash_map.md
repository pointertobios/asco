# `asco::concurrency::hash_map<K, V>`：并发哈希表

`hash_map<K, V>` 提供在并发场景下可用的键值存储，并以“显式失败码 + 可重试”作为主要控制流：当遇到并发冲突或正在重整（rehash）时，操作会返回错误码提示你让出执行权后重试。

## 类型要求

- `K` 必须可比较（`==`）且可被 `std::hash<K>` 哈希。
- `V` 需要满足可移动/可安全搬运（以及 nothrow 析构）。
- 当 `V = void` 时，可作为集合（set）语义使用：只关心 key 是否存在。

## 并发语义

### 1) 线程安全边界

- 多个任务/线程可以并发调用 `try_insert/try_get/try_contains/try_remove/try_rehash` 以及 `insert/get/remove/contains/size`。
- 在并发下，部分调用**可能失败但不破坏容器状态**；调用方需要按错误码进行重试或退避。

### 2) `guard` 的语义（`try_get()` 返回）

- 当 `V != void` 且 `try_get(key)` 成功时返回一个 `guard`。
- **在 `guard` 生命周期内**：该元素不会被 `try_remove()` 或 `try_rehash()` 销毁/搬迁，从而保证通过 `guard` 取得的引用不会悬垂。
- **不要长时间持有 `guard`**：长持有会让 `try_remove()`（对同一 key）以及 `try_rehash()` 更容易失败或等待。

### 3) value 的并发读写

- `hash_map` 仅保证元素生命周期安全；**不保证对同一个 value 的并发修改一定无数据竞争**。
- 若需要并发写同一个 `V`，请让 `V` 自身具备同步语义（如原子/锁/无锁结构），或由外部加锁。

## API 与错误码

### `size()`

返回：`std::size_t`。

- 返回当前元素数量的一个快照。
- 在并发插入/删除过程中，该值可能不是线性一致（可能偏旧）；用于统计、监控或启发式决策。

### `try_insert(key, value)`

返回：`std::expected<std::monostate, insert_failed>`。

- `key_repeated`：key 已存在。
- `rehash_needed`：建议调用 `try_rehash()` 后重试插入。
- `rehashing`：当前有线程/任务正在执行 `try_rehash()`。
- `retry`：遇到瞬态并发冲突，建议退避后重试。

当 `V = void` 时，还提供 `try_insert(key)`（不带 value），表示“插入 key”。

### `try_get(key)`

当 `V != void` 时：返回 `std::expected<guard, get_failed>`。

当 `V = void` 时：返回 `std::expected<std::monostate, get_failed>`。成功表示 key 存在。

- `none`：key 不存在。
- `rehashing`：当前正在 `try_rehash()`。
- `retry`：遇到瞬态并发冲突，建议退避后重试。

### `try_contains(key)`

返回：`std::expected<bool, contains_failed>`。

- 成功时返回 `true/false`，表示 key 是否存在。
- 成功时返回 `true/false`，表示 key 是否存在。
- `try_contains` 不提供元素生命周期保护；返回 `true` 也不意味着后续操作一定不会因并发而失败。
- 失败时返回 `contains_failed`：
  - `rehashing`：当前正在 `try_rehash()`。
  - `retry`：遇到瞬态并发冲突，建议退避后重试。

### `try_remove(key)`

当 `V != void` 时：返回 `std::expected<V, remove_failed>`。

当 `V = void` 时：返回 `std::expected<std::monostate, remove_failed>`。

- `none`：key 不存在。
- `guard_protecting`：有 `guard` 正在保护该元素，暂时无法删除。
- `rehashing`：当前正在 `try_rehash()`。
- `retry`：遇到瞬态并发冲突，建议退避后重试。
- `thrown`：容器内部存在由异常路径引入的不可移除状态（见下文“异常行为”）。

### `try_rehash()`

`try_rehash()` 尝试扩容并重整内部结构：

- 返回 `true`：本次成功执行。
- 返回 `false`：未能开始（例如已有其他 `try_rehash()` 在进行）。
- 当 `try_rehash()` 进行中，`try_insert/try_get/try_remove` 可能返回 `rehashing`。
- `try_rehash()` 可能需要等待相关 `guard` 释放；因此长时间持有 `guard` 会显著影响 `try_rehash()` 的完成时机。

注意：容器刻意不提供“不带 `try_` 的 `rehash()`”。在高并发下，如果 `rehash()` 被设计为“最终必然成功”，很容易在短时间内连续触发多次扩容，导致容量被快速放大、浪费内存。

该容器更推荐的模式是：把 rehash 作为一次性纠偏动作（例如在插入返回 `rehash_needed` 时执行一次 `try_rehash()`，随后立刻重试插入）。只要当前容量已经足够，后续操作就不需要再触发 rehash。

## 便捷接口（非 `try_`）

`hash_map` 额外提供一组“不带 `try_` 前缀”的便捷接口：它们会在内部循环调用 `try_*`，对 `rehashing/retry` 做忙等重试（使用 `cpu_relax()`）。

在协程环境中，若你希望让出执行权而不是忙等，优先使用 `try_*` 并在失败时 `co_await asco::this_task::yield()`。

### `insert(key, value)` / `insert(key)`

- 当 `V != void`：`bool insert(const K&, V&&)`。
- 当 `V = void`：`bool insert(const K&)`。
- 成功返回 `true`。
- 若 key 已存在返回 `false`。
- 遇到 `rehash_needed` 时，会执行一次 `try_rehash()` 并立即重试插入；容量足够后就会停止扩容并完成插入。

### `get(key)`（仅 `V != void`）

返回 `guard`：

- 若 key 存在，返回一个有效 `guard`。
- 若 key 不存在，返回空 `guard`（可用 `if (g) { ... }` 判断）。

### `remove(key)`

- 当 `V != void`：返回 `std::optional<V>`。成功删除返回 `V`；key 不存在返回 `std::nullopt`。
- 当 `V = void`：返回 `bool`。成功删除返回 `true`；key 不存在返回 `false`。

注意：若内部遇到 `remove_failed::thrown`，会触发 panic（表示出现了异常遗留状态，需由更高层处理）。

### `contains(key)`

返回 `bool`：key 存在返回 `true`，不存在返回 `false`。该接口同样会在 `rehashing/retry` 时忙等重试。

## 推荐的重试策略

在协程环境中，遇到 `retry/rehashing` 通常采用：

- `co_await asco::this_task::yield();` 让出执行权
- 或者在更高层做指数退避/限次重试

当遇到 `rehash_needed` 时：

- 尝试调用 `try_rehash()`，成功后再重试 `try_insert()`
- 或者把 `try_rehash` 延迟到更合适的时间窗口（取决于业务延迟目标）

## 异常行为

- `try_insert()` 可能因 `K/V` 的构造或移动而抛异常。
- 在发生异常后：
  - 该次插入不会返回成功。
  - 后续 `try_remove()` 可能返回 `thrown`（表示出现了需要调用方关注的异常遗留状态）。

建议：在高可靠场景中优先使用 nothrow 构造/移动的类型；或在捕获异常后采用更高层的恢复策略（例如重新创建容器并回放数据）。

## 示例（协程风格）

### 插入：处理 `rehash_needed` 与可重试错误

```cpp
for (;;) {
    auto r = m.try_insert(k, v);
    if (r) break;

    switch (r.error()) {
    case decltype(m)::insert_failed::key_repeated:
        co_return; // 或者做更新逻辑
    case decltype(m)::insert_failed::rehash_needed:
        (void)m.try_rehash();
        break;
    case decltype(m)::insert_failed::rehashing:
    case decltype(m)::insert_failed::retry:
        co_await asco::this_task::yield();
        break;
    }
}
```

### 删除：处理 `guard_protecting`

```cpp
for (;;) {
    auto r = m.try_remove(k);
    if (r || r.error() == decltype(m)::remove_failed::none) break;

    if (r.error() == decltype(m)::remove_failed::guard_protecting
        || r.error() == decltype(m)::remove_failed::retry
        || r.error() == decltype(m)::remove_failed::rehashing) {
        co_await asco::this_task::yield();
        continue;
    }

    // remove_failed::thrown 等：按业务决定如何处理
    co_return;
}
```

### 判断存在性：处理可重试错误

```cpp
for (;;) {
    auto r = m.try_contains(k);
    if (r) {
        if (*r) {
            // exists
        } else {
            // not exists
        }
        break;
    }
    co_await asco::this_task::yield();
}
```
