# Raw File

`asco::io::file` provides a direct OS-facing file read/write interface (no user space buffering). It exposes coroutine-based asynchronous APIs suitable for scenarios that need fine-grained I/O control.

Key points

- No user‑space buffering: requests are submitted straight to the kernel
- Asynchronous interface: open / close / reopen / write / read are coroutines; read is abortable & resumable; open / close / reopen / write are unabortable inline futures
- Independent read & write pointers: supports seekg / seekp and tellg / tellp
- Native handle retrievable for interop with other system APIs
- Works well with zero-copy buffer `buffer<>`

## Main Types

- `file`: handle of an opened file
- `file::options`: open flags (bit flags)
- `opener`: chain-configured builder for opening
- `open_file`: low-level open implementation (returns `future<std::expected<file,int>>`)
- `read_result`: read status (`eof` / `interrupted` / `again`)
- `file_state`: file state view (e.g. `size()`)

## Open & Close

Constructing an opener

- `file::at(path)`: create an `opener` from a path
- `file::at()`: create an `opener` from an existing `file` context (handy for `reopen()`)

Open / close methods

- `future_inline<void> open(std::string path, flags<file::options> opts, uint64_t perm = 0)`
- `future<void> close()`
- `future_inline<void> reopen(opener&&)`

Notes

- Calling `open()` again on an already opened object throws
- Provide `mode` (e.g. 0644) when using the `create` option
- Destructor will automatically `close()` if still open

Example

```cpp
using asco::io::file;
using asco::flags;

auto r = co_await file::at("/tmp/data.log")
             .write().append().create().exclusive()
             .mode(0644)
             .open();
if (!r) co_return;                // r.error() carries errno
file f = std::move(*r);

// Or: file f; co_await f.open("/tmp/data.log", flags{file::options::write}, 0644);
co_await f.close();
```

## Read & Write

Write

- Signature: `future<std::optional<buffer<>>> write(buffer<> buf)` (unabortable)
- Semantics: attempts to write out `buf`; if partially written returns remaining data; returns `std::nullopt` when fully written
- Requires `options::write`

Read

- Signature: `future<std::expected<buffer<>, read_result>> read(size_t nbytes)`
- Semantics: reads up to `nbytes`; on success returns data; on condition returns `eof` / `interrupted` / `again`
- Requires `options::read` (abortable)

Example: write until completion

```cpp
asco::io::buffer<> buf("hello");
while (true) {
    auto rest = co_await f.write(std::move(buf));
    if (!rest) break;
    buf = std::move(*rest);
}
```

Example: read until EOF

```cpp
std::string out;
for (;;) {
    auto r = co_await f.read(4096);
    if (!r) {
        if (r.error() == asco::io::read_result::eof) break;
        if (r.error() == asco::io::read_result::interrupted) continue;
        if (r.error() == asco::io::read_result::again) { co_await std::suspend_always{}; continue; }
    } else {
        out += std::move(*r).to_string();
    }
}
```

## Pointers & State

- `size_t seekg(ssize_t offset, seekpos whence = seekpos::current)`
- `size_t seekp(ssize_t offset, seekpos whence = seekpos::current)`
- `size_t tellg() const`
- `size_t tellp() const`
- `file_state state() const` (Linux: via `fstat()`)

Rules

- `seekpos::begin`: `offset` must be non-negative
- `seekpos::end`: `offset` must be non-positive
- `seekpos::current`: positive or negative allowed but must remain in-range
- Out-of-range offsets throw a runtime error

Example

```cpp
f.seekp(0, asco::io::seekpos::end); // append
auto pos = f.tellp();
f.seekg(0, asco::io::seekpos::begin);
```

## Native Handle

- `raw_handle()`: returns underlying native handle (Linux: `int`) for system API interop
