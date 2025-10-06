# Printing utilities (print library)

asco provides thin async wrappers over C++23 `std::format` for writing to the runtime-managed stdout/stderr. The APIs live in `asco/print.h` and return `future<void>`.

- Header: `#include <asco/print.h>`
- Goal: ergonomic formatted output within async coroutines.

## API overview

- `print(fmt, args...)`: write to stdout (no trailing newline)
- `println(fmt, args...)`: write to stdout and append `\n`
- `eprint(fmt, args...)`: write to stderr (no trailing newline)
- `eprintln(fmt, args...)`: write to stderr and append `\n`

Signatures (simplified):

```cpp
future<void> print(std::format_string<Args...> fmt, Args&&... args);
future<void> println(std::format_string<Args...> fmt, Args&&... args);
future<void> eprint(std::format_string<Args...> fmt, Args&&... args);
future<void> eprintln(std::format_string<Args...> fmt, Args&&... args);
```

## Example

```cpp
#include <asco/future.h>
#include <asco/print.h>

using asco::future, asco::println, asco::print, asco::eprintln;

future<int> async_main() {
    println("Hello, {}!", "ASCO");
    int answer = 42;
    print("answer = {}", answer);
    eprintln(" oops: {}", "something went wrong");
    co_return 0;
}
```

## vs std::print

- `std::print`/`std::println` are synchronous and blocking; asco versions are async.
- Integrated with asco runtime I/O for consistent async behavior and testing.
