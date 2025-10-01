# Zero-Copy Buffer (`buffer`)

## Overview

`asco::io::buffer<CharT>` is a zero‑copy buffer type for high‑performance I/O. It allows multiple data segments to be chained, split, transferred, and combined without repeated memory copies. The default character type is `char`, but you can also use `std::byte` for raw binary payloads.

Typical use cases:

- Segmented network / file I/O read & aggregation
- Efficient assembly of logs, protocol frames, messages
- High‑throughput scenarios where extra copies are harmful

Key traits:

- Zero‑copy concatenation from multiple sources (raw memory, `std::string`, `std::string_view`)
- Clear distinction between consuming vs non‑consuming operations (e.g. `to_string` / `split` consume; `clone` / `rawbuffers` do not)
- Plays nicely with the standard library; intuitive API surface

## Quick Start

```cpp
using asco::io::buffer;
using namespace std::literals;

// Append & merge (to_string consumes the buffer)
buffer<> b;
b.push('A');
b.push(std::string("SCO"));
b.push("._TEST"sv);
std::string out = std::move(b).to_string(); // "ASCO._TEST"

// Concatenate another buffer (source cleared)
buffer<> x, y;
x.push("Hello"sv);
y.push("World"sv);
x.push(std::move(y));                // y cleared
std::string s = std::move(x).to_string();   // "HelloWorld"

// Split
buffer<> z("HelloWorld"s);
auto [left, right] = std::move(z).split(5); // "Hello" | "World"

// Clone (non‑consuming, suitable for repeated reads)
buffer<> a("ASCO"s);
auto c = a.clone();
std::string s1 = std::move(c).to_string();         // "ASCO"
std::string s2 = std::move(a.clone()).to_string(); // still readable again

// Overwrite modify (replace from start position; may truncate tail)
buffer<> m("HelloWorld"s);
buffer<> ins("ASCO"s);
m.modify(5, std::move(ins)); // Result "HelloASCOd"
auto rs = std::move(m).to_string();

// Append zero bytes (reserve fixed-size structure padding)
buffer<> zf;
zf.push("A"sv);
zf.fill_zero(3);
zf.push("B"sv);
auto zs = std::move(zf).to_string(); // "A\0\0\0B"

// Iterate raw underlying chunks (zero‑copy send)
buffer<> r("AA"s);
auto it = r.rawbuffers();
while (!it.is_end()) {
    auto rb = *it;  // rb.buffer / rb.size / rb.seq
                    // Directly usable for syscalls or network send
    ++it;
}
```

## Common API (Grouped by Intent)

Construction & Assignment

- `buffer()` / `buffer(CharT)` / `buffer(std::basic_string<CharT>&&)`
- `buffer(const std::basic_string_view<CharT>&)`
- Movable (construction & assignment); copying disabled

Writing & Composition

- `void push(CharT)`
- `void push(std::basic_string<CharT>&&)`
- `void push(const std::basic_string_view<CharT>&)`
- `void push(buffer&&)` (concatenate & clear source)
- `void fill_zero(size_t n)` append n zero bytes / chars
- `void push_raw_array_buffer(CharT* buf, size_t capacity, size_t size, buffer_destroyer* destroyer = nullptr)` attach user memory directly

Reading Out & Splitting

- `std::basic_string<CharT> to_string(this buffer&&)` merge and consume (object cleared)
- `std::tuple<buffer, buffer> split(this buffer&&, size_t pos)` split into two buffers; consumes the source; prior `clone()` results unaffected

Copy & Modify

- `buffer clone() const` non‑consuming clone (shared underlying data)
- `void modify(this buffer&, size_t start, buffer<CharT> buf)` overwrite from `start`; prior `clone()` results unaffected

State & Utilities

- `size_t size() const`, `bool empty() const`, `size_t buffer_count() const`
- `void clear()`, `void swap(buffer& rhs)` (do not affect prior `clone()` objects)
- `rawbuffer_iterator rawbuffers() const` iterate underlying contiguous blocks

## Behavior & Semantics

- Consuming operations: `to_string()`, `split()` clear the object.
- Non‑consuming: `clone()`, `rawbuffers()` leave data intact.
- `push(buffer&&)` transfers and clears the source.
- Passed `std::string_view` lacks ownership — ensure referenced memory stays valid.
- Not thread‑safe.

## Errors & Boundaries

- `split(pos)` and `modify(start, ...)` throw on out‑of‑range indices.
- After `to_string()` / `split()`, the original object is cleared; further reads yield empty data.
- `rawbuffers()` exposes internal chunk pointers valid only while the buffer object (and any shared backing for clones) remains alive.
