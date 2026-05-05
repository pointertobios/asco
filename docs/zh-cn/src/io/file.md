# `asco::io::file`：异步文件访问

`file` 表示一个由 ASCO 运行时管理的文件句柄，提供异步打开、读取和写入能力。

头文件：`asco/io/file.h`

---

## 基本语义

- `file` 不支持复制；支持移动。移动后，目标对象接管文件句柄与当前文件位置，源对象不再表示有效文件。
- 默认构造的 `file` 不表示有效文件。对无效文件调用 `seek(...)`、`read(...)` 或 `write(...)` 会触发 panic。
- `close()` 关闭当前文件句柄。对无效文件或已经关闭的文件再次调用 `close()` 没有效果。
- 通过 `open(...)`、`file::at(...)`、`file::create(...)` 或 `file::from_handle(...)` 得到的有效 `file` 都会在 `close()` 或析构时关闭其持有的句柄。
- `file` 是非并发文件对象；同一个对象的 `seek(...)`、`read(...)`、`write(...)` 和 `close()` 调用应由调用方自行串行化。

---

## 打开文件

### `file::at(path)`

```cpp
auto opened = co_await asco::io::file::at("data.txt").open();

std::filesystem::path path = "data.txt";
auto opened_by_path = co_await asco::io::file::at(path).open();
```

`file::at(path)` 创建一个打开已有文件的构造器。`path` 可以是字符串视图，也可以是 `std::filesystem::path`。默认打开标志包含 `open_flags::read`，不包含 `open_flags::create`。

`open()` 返回 `future<std::expected<file, open_failed>>`：

- 成功时返回一个有效的 `file`，文件位置位于开头。
- 失败时返回 `open_failed`，例如路径不存在、权限不足、路径无效或打开文件数量超限。

### `file::create(path)`

```cpp
auto opened = co_await asco::io::file::create("data.txt")
                  .enable_flag(asco::io::open_flags::write)
                  .enable_flag(asco::io::open_flags::truncate)
                  .open();

std::filesystem::path path = "data.txt";
auto opened_by_path = co_await asco::io::file::create(path)
                           .enable_flag(asco::io::open_flags::write)
                           .open();
```

`file::create(path)` 创建一个带 `open_flags::create` 的打开构造器。`path` 可以是字符串视图，也可以是 `std::filesystem::path`。它默认仍包含 `open_flags::read`；如果需要写入，应显式启用 `open_flags::write`。

构造器可以通过以下接口调整打开行为：

- `enable_flag(flag)` 启用一个 `open_flags`。
- `disable_flag(flag)` 移除一个 `open_flags`。
- `with_mode(mode)` 设置创建文件时使用的 `create_mode` 权限。

构造器只能移动，调用 `open()` 会消费该构造器。

### 成员 `open(...)`

```cpp
asco::io::file f;
auto result = co_await f.open(
    "data.txt",
    asco::io::open_flags::read,
    asco::io::create_mode::read | asco::io::create_mode::write | asco::io::create_mode::read_share);

std::filesystem::path path = "data.txt";
auto path_result = co_await f.open(
    path,
    asco::io::open_flags::read,
    asco::io::create_mode::read | asco::io::create_mode::write | asco::io::create_mode::read_share);
```

成员 `open(path, flags, mode)` 在现有 `file` 对象上打开路径，返回 `future<std::expected<void, open_failed>>`。

`path` 可以是拥有路径内容的 `std::string`、字符串视图或 `std::filesystem::path`。传入字符串视图时，接口会在异步打开前复制路径内容，调用方不需要让原字符串视图跨越 `co_await` 存活。

如果对象当前已经持有文件句柄，会先关闭旧句柄。打开成功后，对象表示新文件，文件位置重置到开头；打开失败时返回 `open_failed`。

### `file::from_handle(handle)`

```cpp
int fd = /* 已打开的 Unix 文件描述符 */;
auto f = asco::io::file::from_handle(asco::core::os::io_handle_from_unix_fd(fd));
```

`file::from_handle(handle)` 从已有 `io_handle` 构造一个有效的 `file` 对象。构造完成后，`file` 接管该句柄：后续调用 `close()`、移动赋值替换句柄或析构 `file` 时，都会关闭它当前持有的句柄。

调用方需要保证传入的 `io_handle` 满足以下条件：

- 句柄必须已经打开，并且适用于当前运行时的 I/O 后端。
- 句柄的访问权限需要与后续 `read(...)` 或 `write(...)` 操作匹配。
- 句柄不应再由调用方重复关闭；需要继续手动管理的句柄不应交给 `file::from_handle(...)`。

`file::from_handle(...)` 不执行异步打开，也不返回 `expected`。如果传入的句柄无效，后续读取、写入、定位或关闭时会按底层 I/O 后端的行为失败或触发 panic。

平台原生句柄需要先转换为 `io_handle`：

- Unix 文件描述符使用 `asco::core::os::io_handle_from_unix_fd(fd)`。
- Windows 原生句柄使用 `asco::core::os::io_handle_from_windows_handle(handle)`。

只应使用当前平台支持的转换函数；不匹配的平台转换不提供可移植语义。

---

## 文件位置

`file` 维护一个当前位置。成功的 `read(...)` 和 `write(...)` 会按实际读取或写入的字节数推进当前位置。

```cpp
f.seek(0, asco::io::seek_mode::set);
```

`seek(offset, mode)` 同步更新当前位置：

- `seek_mode::set` 以文件开头为基准；负偏移会定位到开头。
- `seek_mode::current` 以当前位置为基准；向前越过文件开头时会定位到开头。
- `seek_mode::end` 以文件末尾为基准；向前越过文件开头时会定位到开头。

---

## 读取

```cpp
auto data = co_await f.read(4096);
```

`read(size)` 返回 `future<std::expected<buffer<>, read_failed>>`。

- 成功时返回一个 `buffer<>`。`buffer::size()` 表示本次请求的容量，`buffer::cursor()` 表示实际读取的字节数。
- 短读和 EOF 都属于成功结果；到达 EOF 时 `cursor()` 为 `0`。
- 成功读取后，文件位置按实际读取字节数前进。
- 文件句柄无效、缓冲区无效或权限不满足时返回 `read_failed`。

读取结果的有效数据范围是 `[data(), data() + cursor())`。

---

## 写入

```cpp
asco::io::buffer<> data{"hello"};
auto written = co_await f.write(data);
```

`write(buf)` 返回 `future<std::expected<std::size_t, write_failed>>`。

- 写入的数据范围是 `buf` 的有效字节范围，即 `[buf.data(), buf.data() + buf.cursor())`。
- 成功时返回实际写入的字节数，并按该字节数推进文件位置。
- 文件句柄无效、缓冲区无效或权限不满足时返回 `write_failed`。

调用方必须保证传入的 `buffer<>` 在异步写入完成前保持存活，且这段有效字节内容不会被并发修改。

---

## 使用建议

- 需要写入时显式启用 `open_flags::write`；`file::create(...)` 并不自动启用写权限。
- 对 `open(...)`、`read(...)` 和 `write(...)` 的 `expected` 结果做错误检查，不要假设文件系统操作一定成功。
- 对同一个 `file` 对象串行执行位置相关操作；并发读写同一个对象会让当前位置语义变得不确定。
- 读取返回后用 `cursor()` 判断有效字节数，尤其要处理短读和 EOF。
