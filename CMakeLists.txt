# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.10)
project(asco)

# Compiler and linker config

include(ProcessorCount)

ProcessorCount(CPUS)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)
if (NOT WIN32)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-gnu-statement-expression -Wno-gnu-statement-expression-from-macro-expansion)
    link_libraries(atomic)
endif()
add_compile_options(-Wno-vla-cxx-extension -Wno-zero-length-array)

if (UNIX AND NOT APPLE AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    find_program(LD_LLD  NAMES ld.lld)
    find_program(LD_MOLD NAMES ld.mold)

    set(_picked_ld "")
    if (LD_LLD)
        set(_picked_ld "lld")
        message(STATUS "Linker: using lld (${LD_LLD})")
    elseif (LD_MOLD)
        set(_picked_ld "mold")
        message(STATUS "Linker: using mold (${LD_MOLD})")
    else()
        message(STATUS "Linker: using system ld")
    endif()

    if (_picked_ld)
        set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=${_picked_ld}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=${_picked_ld}")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=${_picked_ld}")

        if (_picked_ld STREQUAL "mold" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND LD_MOLD)
            set(CMAKE_LINKER "${LD_MOLD}" CACHE FILEPATH "Path to linker" FORCE)
        endif()
        if (_picked_ld STREQUAL "lld" AND LD_LLD)
            set(CMAKE_LINKER "${LD_LLD}" CACHE FILEPATH "Path to linker" FORCE)
        endif()

        add_link_options("-Wl,--threads=${CPUS}")
    endif()
else()
    message(STATUS "Linker: using system ld")
endif()

enable_testing()

# ASCO config

option(ASCO_PERF_RECORD "ASCO Performance Recording" OFF)
option(ASCO_IO_URING "io_uring support for linux" ON)
option(ASCO_IOCP "IOCP support for windows" OFF)

if (WIN32 AND ASCO_IO_URING)
    error("ASCO_IO_URING must be off on windows target.")
endif()

if (LINUX AND ASCO_IOCP)
    error("ASCO_IOCP must be off on linux target.")
endif()

add_subdirectory(asco)
add_subdirectory(tests)
