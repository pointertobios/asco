# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set(ASCO_SOURCES
    runtime.cpp
    sched.cpp
)

add_library(asco-main asco_main.cpp)
target_include_directories(asco-main PRIVATE ../)

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
target_link_libraries(asco atomic)

add_library(asco-shared SHARED ${ASCO_SOURCES})
target_include_directories(asco-shared PUBLIC ../)
set_target_properties(asco-shared PROPERTIES OUTPUT_NAME asco)

