# Stream Reader (`stream_reader`)

`stream_reader<T>` is a helper for sequential, read‑only data sources. It exposes three categories of operations:

- Fixed-size (read up to N bytes): `read`
- Read the remainder until EOF: `read_all`
- Incremental line reading: `read_lines`

## Common APIs

| Method                                                                                     | Purpose                                                                                                    | Return                   |
| ------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------- | ------------------------ |
| `read(size_t n)`                                                                           | Read at most n bytes (may be fewer if EOF reached)                                                         | `future<buffer<>>`       |
| `read_all()`                                                                               | Read everything until EOF (single shot; beware huge streams)                                               | `future<buffer<>>`       |
| `read_lines(newline nl, std::optional<future_inline<mutex<>::guard>> lock = std::nullopt)` | Incremental line-by-line read (supports LF / CR / CRLF); optionally hold a mutex during generator lifetime | `generator<std::string>` |

Newline enum values: `newline::LF`, `newline::CR`, `newline::CRLF`.

Behavior details:

- LF mode: split by `\n`, the delimiter is not included in the line.
- CR mode: split by `\r`, the delimiter is not included.
- CRLF mode: prefer `\r\n` as the newline; a bare `\r` (not followed by `\n`) is preserved as data.
- Consecutive delimiters produce empty lines (e.g., `"A\r\n\r\nB"` -> `["A", "", "B"]`).
- At EOF, if there is a pending tail without a trailing newline, it is yielded as the last line.

Lock parameter:

- `read_lines(nl, mtx.lock())` will keep the mutex locked while the generator is alive; it releases when the generator is exhausted/destroyed.
- While the generator is alive, `mtx.try_lock()` fails; after completion, it succeeds.

Abort recovery semantics:

- If the generator is aborted, any lines that have been `co_yield`ed but not yet received by the caller are cached back into the `stream_reader`.
- A subsequent `read_lines` will continue from these cached lines, ensuring that "after abort, the previously yielded but unconsumed content is delivered first".

## Quick Start

```cpp
stream_reader sr{std::move(sock)};          // Assume sock satisfies stream_read concept

// 1. Read a fixed-size header
auto head = co_await sr.read(8);
auto len  = parse_len(head);

// 2. Read the body
auto body = co_await sr.read(len);

// 3. Read the remaining content (if protocol allows)
auto remain = co_await sr.read_all();

// 4. Read line-by-line (e.g. text segment)
auto g = sr.read_lines();
while (auto line = co_await g()) {
    handle_line(line);
}
```

## Line Reading Example (CRLF, typical HTTP headers)

```cpp
std::vector<std::string> headers;
auto g = sr.read_lines(newline::CRLF);
while (auto line = co_await g()) {
    if (line.empty()) break;   // Empty line = end of header section
    headers.push_back(std::move(line));
}
```

## Lock and abort examples

Holding a lock:

```cpp
mutex<> mtx;
auto gen = sr.read_lines(newline::lf, mtx.lock());
// While the generator is alive, try_lock should fail
assert(!mtx.try_lock().has_value());
// Drain the generator
std::vector<std::string> lines;
while (auto s = co_await gen()) lines.push_back(*s);
// After completion, the lock becomes available again
assert(mtx.try_lock().has_value());
```

Abort recovery:

```cpp
auto gen = sr.read_lines(newline::lf);
auto first = co_await gen();      // e.g., "L1"
gen.abort();                      // abort; already-yielded-but-unreceived lines are cached back

auto gen2 = sr.read_lines(newline::lf);
while (auto s = co_await gen2()) {
    // Continues from the lines yielded before the abort
}
```

## Return Semantics

You only need to check: when a `read(n)` result's `buffer` size is smaller than the requested n and subsequent calls keep returning empty, you have reached EOF.
