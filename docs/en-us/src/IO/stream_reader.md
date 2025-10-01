# Stream Reader (`stream_reader`)

`stream_reader<T>` is a helper for sequential, read‑only data sources. It exposes three categories of operations:

- Fixed-size (read up to N bytes): `read`
- Read the remainder until EOF: `read_all`
- Incremental line reading: `read_lines`

## Common APIs

| Method                   | Purpose                                                      | Return                   |
| ------------------------ | ------------------------------------------------------------ | ------------------------ |
| `read(size_t n)`         | Read at most n bytes (may be fewer if EOF reached)           | `future<buffer<>>`       |
| `read_all()`             | Read everything until EOF (single shot; beware huge streams) | `future<buffer<>>`       |
| `read_lines(newline nl)` | Incremental line-by-line read (supports LF / CR / CRLF)      | `generator<std::string>` |

Newline enum values: `newline::LF`, `newline::CR`, `newline::CRLF`.

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

## Return Semantics

You only need to check: when a `read(n)` result's `buffer` size is smaller than the requested n and subsequent calls keep returning empty, you have reached EOF.
