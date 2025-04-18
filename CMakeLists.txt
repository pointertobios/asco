# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)
project(asco)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(CMAKE_CXX_FLAGS -fuse-ld=lld)

# add_compile_options("-stdlib=libc++" "-lc++abi" "-v")

enable_testing()

add_subdirectory(asco)

add_subdirectory(tests)
