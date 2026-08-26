# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

include(${PROJECT_SOURCE_DIR}/cmake/GeneratePch.cmake)

asco_generate_pch(PRECOMPILE_HEADERS_COMMON
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include
    EXCLUDE
        asco/test
        asco/debug
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pch.h
)

asco_generate_pch(PRECOMPILE_HEADERS_TEST
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/test
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pch_test.h
)

asco_generate_pch(PRECOMPILE_HEADERS_DEBUG
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/debug
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pch_debug.h
)

set(PRECOMPILE_HEADERS ${PRECOMPILE_HEADERS_COMMON})
if (ASCO_DEBUG_ENABLED)
    set(PRECOMPILE_HEADERS ${PRECOMPILE_HEADERS} ${PRECOMPILE_HEADERS_DEBUG})
endif()

set(ASCO_SOURCES
    src/core/blocking_worker.cpp
    src/core/daemon.cpp
    src/core/runtime.cpp
    src/core/scheduler.cpp
    src/core/worker.cpp
    src/os/thread.cpp
    src/sync/condition_variable.cpp
)
if (ASCO_DEBUG_ENABLED)
    set(ASCO_SOURCES ${ASCO_SOURCES}
        src/debug/debug_host.cpp
        src/future.cpp
    )
endif()

set(MDEFS ASCO __ASCO__)
if (ASCO_DEBUG_ENABLED)
    set(MDEFS ${MDEFS} ASCO_DEBUG_ENABLED)
endif()

set(DEPLIBS)

if (USING_LIBCXX)
    set(MDEFS ${MDEFS} USING_LIBCXX)
endif()

if (LINUX)
    set(ASCO_SOURCES ${ASCO_SOURCES}
        src/os/linux/terminal.cpp
        src/os/linux/thread.cpp
    )

    if (ASCO_IO_URING)
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )

        set(DEPLIBS ${DEPLIBS} uring)
    endif()

    set(DEPLIBS ${DEPLIBS} atomic ncurses)
elseif (WIN32)
    set(ASCO_SOURCES ${ASCO_SOURCES}
        src/os/windows/terminal.cpp
        src/os/windows/thread.cpp
    )

    if (ASCO_IOCP)
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )

        set(DEPLIBS ${DEPLIBS} Advapi32)
    endif()
endif()

if (NOT ASCO_IO_URING AND NOT ASCO_IOCP)
endif()

if (ASCO_IO_URING)
    set (MDEFS ${MDEFS} ASCO_IO_URING)
endif()
if (ASCO_IOCP)
    set (MDEFS ${MDEFS} ASCO_IOCP)
endif()

if (ASCO_PERF_ENABLED)
    set (MDEFS ${MDEFS} ASCO_PERF_ENABLED)
endif()

set(COMPILE_OPTIONS
)

set(LINK_OPTIONS
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-zero-length-array
    )
    
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
            -Wno-interference-size
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
            -ferror-limit=9999999
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        /W4
        /WX
        /utf-8
        /Zc:preprocessor
        /Zc:lambda
    )
endif()

add_library(asco_core ${ASCO_SOURCES})
target_include_directories(asco_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(asco_core PUBLIC ${DEPLIBS})
target_link_options(asco_core PUBLIC ${LINK_OPTIONS})
target_compile_definitions(asco_core PUBLIC ${MDEFS})
target_compile_options(asco_core PUBLIC ${COMPILE_OPTIONS})
target_precompile_headers(asco_core PUBLIC ${PRECOMPILE_HEADERS})
add_library(asco::core ALIAS asco_core)

add_library(asco_main src/asco_main.cpp)
target_link_libraries(asco_main PUBLIC asco_core)
add_library(asco::main ALIAS asco_main)

# Test library

add_library(asco_test src/test/test_main.cpp)
target_link_libraries(asco_test PUBLIC asco::core)
target_compile_definitions(asco_test PUBLIC ASCO_TESTING)
target_precompile_headers(asco_test PUBLIC ${PRECOMPILE_HEADERS_TEST})
add_library(asco::test ALIAS asco_test)
