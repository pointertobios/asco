# `asco::io::buffer<Alloc>`：字节缓冲区

`buffer<Alloc>` 表示一段可拥有的连续字节存储，并用 `cursor()` 记录当前已经写入或可读取的有效字节数。

头文件：`asco/io/buffer.h`

---

## 基本语义

- `size()` 返回当前分配的缓冲区容量，单位为字节。
- `cursor()` 返回当前有效字节数。
- `data()` 返回缓冲区起始地址；当缓冲区为空时返回空指针。
- `push(...)` 只会写入当前剩余容量能够容纳的字节，并返回实际写入的字节数。
- `clear()` 释放当前存储，并把 `size()`、`cursor()` 与 `data()` 重置为空状态。

`buffer` 不支持复制；支持移动。移动后，目标对象接管原有内容，源对象回到空状态并仍可继续使用。

---

## 构造

### 默认构造

```cpp
asco::io::buffer<> buf;
```

默认构造得到空缓冲区：容量为 `0`，有效字节数为 `0`，`data()` 为空。

第一次向空缓冲区 `push(...)` 时，会按本次输入大小分配存储，并把输入全部写入。

### 指定分配器的空缓冲区

```cpp
std::pmr::monotonic_buffer_resource resource;
std::pmr::polymorphic_allocator<std::byte> alloc{&resource};

asco::io::buffer<std::pmr::polymorphic_allocator<std::byte>> buf{alloc};
```

只传入 allocator 时会得到空缓冲区：容量为 `0`，有效字节数为 `0`，`data()` 为空。

该构造不会立即分配存储；后续首次写入或扩容时使用传入的 allocator 分配内存。allocator 可以以引用形式传入，也可以以临时对象形式传入。

### 指定容量

```cpp
asco::io::buffer<> buf{4096};
```

指定容量构造会分配 `size` 字节存储，但有效字节数仍为 `0`。后续 `push(...)` 从缓冲区起始位置写入，直到剩余容量耗尽。

### 从字符串视图构造

```cpp
asco::io::buffer<> buf{std::string_view{"hello"}};
```

字符串视图构造会复制输入字节。构造完成后，`size()` 与 `cursor()` 都等于字符串长度；后续修改原字符串不会影响缓冲区内容。

---

## 写入数据

### `push(std::span<const std::byte>)`

写入 span 中的字节，并返回实际写入数量。

若输入长度大于剩余容量，只写入前缀部分；超出的字节会被忽略。

### `push(std::string_view)`

按字符串视图中的原始字节写入。该接口不附加终止符。

### `push(const buffer&)`

从另一个缓冲区复制其有效字节范围，即 `[data(), data() + cursor())`。

目标缓冲区仍遵循剩余容量限制：如果目标剩余空间不足，只复制能够容纳的前缀。

---

## 预留空间

```cpp
buf.reserve(1024);
```

`reserve(size)` 确保从当前 `cursor()` 之后至少可以继续写入 `size` 字节。

- 如果当前剩余容量已经足够，调用不会改变缓冲区状态。
- 如果剩余容量不足，会分配新的存储，保留已有有效字节，并保持 `cursor()` 不变。
- 扩容后，`size()` 至少为 `cursor() + size`。

---

## 分配器

`buffer<Alloc>` 使用 `Alloc` 管理存储。默认分配器来自 ASCO 的 polymorphic memory resource。

可以传入外部分配器引用，也可以传入临时分配器对象。allocator 可以用于空缓冲区、指定容量构造或字符串构造：

```cpp
std::pmr::monotonic_buffer_resource resource;
std::pmr::polymorphic_allocator<std::byte> alloc{&resource};

asco::io::buffer<std::pmr::polymorphic_allocator<std::byte>> empty{alloc};
asco::io::buffer<std::pmr::polymorphic_allocator<std::byte>> buf{4096, alloc};
```

缓冲区移动后，存储仍由与该存储匹配的分配器释放。

---

## 使用建议

- 用 `cursor()` 判断当前有效数据长度，而不是把 `size()` 当作有效字节数。
- 对 `push(...)` 的返回值做检查；返回值可能小于输入长度。
- 需要连续追加大量数据时，先调用 `reserve(...)` 预留足够的剩余容量。
- `data()` 暴露原始字节指针；调用方需要自行避免越过 `cursor()` 读取无效内容。
