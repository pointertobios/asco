# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.10)
project(asco)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)
if (NOT WIN32)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-gnu-statement-expression -Wno-gnu-statement-expression-from-macro-expansion)
endif()

enable_testing()

option(ASCO_PERF_RECORD "ASCO Performance Recording" OFF)

add_subdirectory(asco)
add_subdirectory(tests)
