# ASCO

[![MIT License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.md)

C++20 coroutine based async framework (***C++23*** needed).

## Getting Started

```c++
#include <print>
#include <asco/future.h>

using asco::future, asco::future_spawn;

future<int> non_spawn_foo() {
    co_return 42;
}

future_spawn<void> spawn_bar() {
    std::println("Calling spawn coroutine");
    return;
}

future<int> async_main() {
    std::println("Hello, World!", co_await non_spawn_foo());
    co_await spawn_bar();
    co_return 0;
}
```

### Documentations

- [简体中文](https://pointertobios.github.io/asco/zhcn/)

### Import into your project

#### Use as submodule

1. Clone one of this repository's version tag.
2. Use with cmake:

- Static link:

```cmake
add_subdirectory(<path-to-this-repo>)
target_link_libraries(<your-target> PRIVATE asco::core asco::main)
```

- Dynamic link:

```cmake
add_subdirectory(<path-to-this-repo>)
target_link_libraries(<your-target> PRIVATE asco::shared::core asco::shared::main)
```

#### Use with cmake FetchContent_Declare()

- How to import

```cmake
include(FetchContent)

FetchContent_Declare(
    asco
    GIT_REPOSITORY https://github.com/pointertobios/asco.git
    GIT_TAG <version-tag>
)
FetchContent_MakeAvailable(asco)

```

- Static link:

```cmake
target_link_libraries(<your-target> PRIVATE asco::core asco::main)
```

- Dynamic link:

```cmake
target_link_libraries(<your-target> PRIVATE asco::shared::core asco::shared::main)
```

## Features

- [x] Basic async runtime
  - [x] Linux full support
  - [ ] Windows full support
  - [ ] MacOS full support
- [ ] Basic tools for parallel programming
  - [ ] Sync Primitives
    - [x] Spin
    - [x] Semaphore
    - [x] Channel
    - [ ] Mutex
    - [ ] Read-Write Lock
    - [ ] Condition variable
    - [ ] Barrier
  - [x] Timer and Interval
- [ ] Async IO
  - [ ] Buffred IO
  - [ ] Async file IO
  - [ ] Async TCP/UDP stream
- [ ] No lock data structures and parallel algorithms
  - [x] continuous_queue
  - and so on...

## Compilers

- Clang: Fully supported.
- GCC: Not complete.
- MSVC: Not supported.

## Development & Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) for details.

## License

This project is licensed under the **MIT License**.

Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>

For full legal terms, see the [LICENSE](./LICENSE.md) file.
