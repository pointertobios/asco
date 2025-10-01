# Stream Writer (`stream_writer`)

`stream_writer<T>` is a line-buffered (manually flushable) helper for sequential output. For an underlying object that satisfies the `stream_write` concept it provides three operations:

- Append buffered write: `write`
- Force write out current buffer: `flush`
- Append then immediately write everything: `write_all`

## Common APIs

| Method                    | Purpose                                                                                            | Return                |
| ------------------------- | -------------------------------------------------------------------------------------------------- | --------------------- |
| `write(buffer<> buf)`     | Append data to internal buffer; if the (accumulated) data sees first `\n`, flush through that line | `future_inline<void>` |
| `write_all(buffer<> buf)` | Append then immediately flush the whole buffer                                                     | `future_inline<void>` |
| `flush()`                 | Write out current buffer (loops on partial writes / remainders)                                    | `future<void>`        |

Behavior notes:

1. `write` only actually hits the underlying stream when a newline `\n` is encountered (flushing inclusive of that newline) or when `flush` is called.
2. Data not ending with `\n` is retained in the internal buffer; destructor performs an automatic `flush()`.
3. If the underlying `write` yields a partial write (returns a remainder `buffer`), the writer will loop internally until completion — callers don't need to handle it.
4. `write_all` is effectively `write` plus an immediate `flush`.

## Quick Start

```cpp
stream_writer sw{std::move(sock)};  // sock satisfies stream_write

// 1. Line-mode output: newline triggers flush
co_await sw.write(buffer<>(std::string_view{"Hello, "));
co_await sw.write(buffer<>(std::string_view{"World!\n"})); // Flushes "Hello, World!\n"

// 2. Data without trailing newline stays buffered
co_await sw.write(buffer<>(std::string_view{"TailPart"}));

// 3. Force output now
co_await sw.flush();  // Writes "TailPart"

// 4. One-shot append & flush
co_await sw.write_all(buffer<>(std::string_view{"AnotherLine\n"}));
```

## Typical Use: Line-based Logging

```cpp
auto log_line = [&](std::string_view s) -> future<void> {
    std::string buf;
    buf.reserve(s.size() + 1);
    buf.append(s);
    buf.push_back('\n');
    co_await sw.write(buffer<>(std::move(buf)));  // Auto flush at newline
};
```

## Destructor Semantics

Destruction triggers an implicit `flush()`:

- Ensures the last unterminated line (no trailing `\n`) is written out.
- For precise control of latency or error handling, you should still explicitly `flush()` at key points.

## Return & Error Model

The returned `future` / `future_inline` only signals asynchronous completion; it does not carry a byte count. Failures (exceptions, fatal errors) propagate via exception or the specific mechanism of the underlying `T`.

## When to Use

- You want to aggregate by line (or small phases) to reduce syscalls.
- You want an automatically flushed buffer on destruction.

## Tips

- If your output lacks newlines yet must be sent promptly, remember to `flush()`.
- A very long single line (no `\n` for a long time) will accumulate memory; manually `flush()` occasionally to bound growth.
