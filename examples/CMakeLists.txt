# Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(base base.cpp)
target_link_libraries(base PRIVATE asco::core asco::main)
