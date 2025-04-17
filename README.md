# ASCO

[![GPLv3 License](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

C++20 coroutine based async framework.

## Compilers

- Clang: Fully supported.
- GCC: Currently not complete, there are some bugs with the compiler generated code.
- MSVC: Not tested.

## Features

- [x] Async runtime
  - [x] Linux full support
  - [ ] Windows full support
  - [ ] MacOS full support
- [x] Basic tools for parallel programming
  - [ ] Timer
  - [x] Semaphore
  - [ ] Condition variable
  - [ ] Locks
  - [ ] Pipe
- [ ] Async IO
  - [ ] Async file read/write
  - [ ] Async TCP/UDP stream

## Getting Started

### Import into your project

1. Clone one of this repository's version tag branch.
2. Import with cmake:

- static link:

```cmake
add_subdirectory(<path-to-this-repo>)
target_link_libraries(<your-target> PRIVATE asco)
```

- dynamic link:

```cmake
add_subdirectory(<path-to-this-repo>)
target_link_libraries(<your-target> PRIVATE asco-shared)
```

### Documentations

- [简体中文](https://pointertobios.github.io/asco/zhcn/)
- [English (May NOT be Latest)](https://pointertobios.github.io/asco/enus/)

## License

This project is licensed under the **GNU General Public License v3.0** (GPLv3).

Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>

For full legal terms, see the [LICENSE](./LICENSE.md) file.
