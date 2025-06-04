# ASCO

[![MIT License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.md)

C++20 coroutine based async framework (***C++23*** needed).

## Getting Started

### Documentations

- [简体中文](https://pointertobios.github.io/asco/zhcn/)

### Import into your project

1. Clone one of this repository's version tag branch.
2. Import with cmake:

- static link:

```cmake
add_subdirectory(<path-to-this-repo>)
target_link_libraries(<your-target> PRIVATE asco asco-main)
```

- dynamic link:

```cmake
add_subdirectory(<path-to-this-repo>)
target_link_libraries(<your-target> PRIVATE asco-shared asco-main)
```

## Features

- [x] Async runtime
  - [x] Linux full support
  - [ ] Windows full support
  - [ ] MacOS full support
- [x] Basic tools for parallel programming
  - [x] Sync Primitives
    - [x] Spin
    - [x] Mutex
    - [x] Read-Write Lock
    - [x] Semaphore
    - [x] Condition variable
    - [x] Barrier
  - [x] Pipe(Channel)
  - [x] Timer and Interval
- [ ] Async IO
  - [ ] Async file read/write
  - [ ] Async TCP/UDP stream

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
