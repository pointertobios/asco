# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set(ASCO_SOURCES
    core/runtime.cpp
    core/sched.cpp
    core/timer.cpp
    futures.cpp
)

add_library(asco-main asco_main.cpp)
target_include_directories(asco-main PRIVATE ../)

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
if (NOT WIN32)
    target_link_libraries(asco atomic)
endif()

add_library(asco-shared SHARED ${ASCO_SOURCES})
target_include_directories(asco-shared PUBLIC ../)
if (NOT WIN32)
    set_target_properties(asco-shared PROPERTIES OUTPUT_NAME asco)
endif()
