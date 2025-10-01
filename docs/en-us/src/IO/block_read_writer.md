# Block Reader/Writer (`block_read_writer`)

`block_read_writer<T>` provides a lightweight user‑space buffer plus independent read/write pointer manager for "block style" I/O.
It maintains an active buffer region and separate read / write positions between the caller and a raw underlying I/O object (e.g. `file`).
This makes sequential + random access easier while preserving the controllability of the underlying raw (unbuffered) I/O:
data written before `flush()` is only visible to this wrapper; only after `flush()` is it persisted to the lower layer.

Key Points

- Unified interface: sequential & random read/write with independent `seekg/seekp` and `tellg/tellp`
- Explicit persistence: data lives only in memory until `flush()`; then actually written to underlying I/O
- Read visibility: unflushed writes are visible to subsequent reads via the same `block_read_writer`
- Auto flush on destruction: destructor calls `flush()` attempting to persist buffered writes
- Coroutine semantics: `read()` is abortable; `write()` and `flush()` are unabortable
- Template constraint: `T` must satisfy the `block_read_write` concept (e.g. `asco::io::file`)

## Quick Start

```cpp
using asco::io::file;
using asco::io::buffer;
using asco::io::seekpos;
using asco::block_read_writer;

auto r = co_await file::at("/tmp/demo.bin")
        .read()
        .write()
        .create()
        .truncate()
        .mode(0644)
        .open();
if (!r) co_return;
block_read_writer<file> brw(std::move(*r));

// Write two fragments (still only in brw's in‑memory buffer)
co_await brw.write(buffer("Hello"));
co_await brw.write(buffer("World"));
assert(brw.tellp() == 10);

// Read them back (unflushed writes are visible within the same wrapper)
brw.seekg(0, seekpos::begin);
auto b = co_await brw.read(1024);
std::string s = b ? std::move(*b).to_string() : "";

// Explicitly persist to underlying I/O (recommended before scope ends)
co_await brw.flush();
```

## API Overview

Template

- `template<block_read_write T> class block_read_writer;`
  - Example: `block_read_writer<asco::io::file>`

Construction / Destruction

- `block_read_writer(T&& ioo)`
- `block_read_writer(const T& ioo)`
- `~block_read_writer()` — calls `flush()` to attempt buffer persistence

Read / Write / Buffer

- `future<std::optional<buffer<>>> read(size_t nbytes)`
  - On success returns data; returns `std::nullopt` at EOF
  - Abortable
- `future<void> write(buffer<> buf)`
  - Updates the in‑memory active buffer & write pointer only; unabortable
- `future<void> flush()`
  - Writes the active buffer (from `buffer_start`) to the underlying I/O; unabortable

Positioning & Queries

- `size_t seekg(ssize_t offset, seekpos whence = seekpos::current)`
- `size_t seekp(ssize_t offset, seekpos whence = seekpos::current)`
- `size_t tellg() const noexcept`
- `size_t tellp() const noexcept`

## Behavior & Semantics

- Write Visibility
  - Unflushed writes are visible to subsequent `read()` calls via the same wrapper
  - Only after `flush()` do they reach the underlying I/O object
- Read / Write Pointers
  - `tellg` / `tellp` track read vs write progress
  - `seekg` / `seekp` are independent and do not affect each other
- Abort Semantics
  - `read()` may be aborted while waiting; `write()` & `flush()` are unabortable
- Destruction
  - The destructor invokes `flush()` to persist remaining buffered data

## Example: Overwrite & Sparse Write

```cpp
// Initial content
co_await brw.write(buffer("HelloWorld"));
co_await brw.flush();

// Overwrite middle 4 bytes with "ASCO" (total length unchanged)
brw.seekp(6, seekpos::begin);
co_await brw.write(buffer("ASCO"));

// Create a 10‑byte hole then write 'X'
brw.seekp(20, seekpos::begin);
co_await brw.write(buffer("X"));

// Before flush, reading through brw sees the modifications:
brw.seekg(0, seekpos::begin);
std::string all;
while (auto part = co_await brw.read(4096)) all += std::move(*part).to_string();

co_await brw.flush(); // Persist to underlying
```

## Dependencies & Concepts

- Include headers: `<asco/io/bufio.h>`, `<asco/io/buffer.h>`
- `T` must satisfy the `block_read_write` concept (e.g. `asco::io::file`)

## Notes

- Not thread‑safe
- Until `flush()`, other objects / processes cannot observe your writes through the underlying I/O
