# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(bench_channel channel.cpp)

target_link_libraries(bench_channel PRIVATE asco::core asco::base)
