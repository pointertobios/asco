# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set(ASCO_SOURCES
    core/runtime.cpp
    core/sched.cpp
    core/timer.cpp
    futures.cpp
    unwind/unwind.cpp
)

if (LINUX)
    set(ASCO_SOURCES ${ASCO_SOURCES} unwind/linux_unwind.cpp)
endif()

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
if (ASCO_PERF_RECORD)
    target_compile_definitions(asco PUBLIC ASCO_PERF_RECORD)
endif()
if (LINUX)
    target_link_libraries(asco PRIVATE dw elf z)
endif()

add_library(asco-shared SHARED ${ASCO_SOURCES})
target_include_directories(asco-shared PUBLIC ../)
if (ASCO_PERF_RECORD)
    target_compile_definitions(asco-shared PUBLIC ASCO_PERF_RECORD)
endif()
if (NOT WIN32)
    set_target_properties(asco-shared PROPERTIES OUTPUT_NAME asco)
endif()

add_library(asco-main asco_main.cpp)
target_link_libraries(asco-main PRIVATE asco)
