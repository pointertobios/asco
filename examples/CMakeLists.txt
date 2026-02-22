# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(common common.cpp)
target_link_libraries(common PRIVATE asco::core asco::main)
