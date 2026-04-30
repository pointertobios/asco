# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

set(ASCO_SOURCES
    core/cancellation.cpp
    core/daemon.cpp
    core/mm/pool.cpp
    core/os/process.cpp
    core/task/dynprio_scheduler.cpp
    core/task/execution_domain.cpp
    core/task/executor.cpp
    core/time/high_resolution_timer.cpp
    core/runtime.cpp
    core/worker.cpp
    this_task.cpp
    time/interval.cpp
    time/sleep.cpp
    yield.cpp
)

set(PRECOMPILE_HEADERS
    cancellation.h
    concurrency/concurrency.h
    concurrency/hash_map.h
    concurrency/ring_queue.h
    core/cancellation.h
    core/daemon.h
    core/mm/pool.h
    core/os/process.h
    core/os/terminal.h
    core/task/cycle_scheduler.h
    core/task/dynprio_scheduler.h
    core/task/execution_domain_proxy.h
    core/task/execution_domain.h
    core/task/executor.h
    core/task/scheduler.h
    core/time/high_resolution_timer.h
    core/time/timer.h
    core/runtime.h
    core/worker.h
    future.h
    invoke.h
    join_handle.h
    panic.h
    sync/channel.h
    sync/condition_variable.h
    sync/mutex.h
    sync/rwlock.h
    sync/semaphore.h
    sync/spinlock.h
    sync/spinrwlock.h
    test/bench.h
    test/test.h
    this_task.h
    task/fetch_result.h
    task/join_all.h
    task/join_set.h
    task/select.h
    time/interval.h
    time/sleep.h
    util/compile_config.h
    util/consts.h
    util/erased.h
    util/murmur.h
    util/raw_storage.h
    util/safe_erased.h
    util/tsc.h
    util/type_id.h
    util/types.h
    yield.h
)

set(MDEFS ASCO __ASCO__
    $<$<CONFIG:Debug>:ASCO_DEBUG_ENABLED>
    $<$<CONFIG:RelWithDebInfo>:ASCO_DEBUG_ENABLED>
)
set(DEPLIBS)

if (USING_LIBCXX)
    set(MDEFS ${MDEFS} USING_LIBCXX)
endif()

if (LINUX)
    set(ASCO_SOURCES ${ASCO_SOURCES}
        core/os/linux/process.cpp
        core/os/linux/terminal.cpp
    )

    if (ASCO_IO_URING)
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )
    else()
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )
    endif()

    set(DEPLIBS ${DEPLIBS} ncurses)
elseif (WIN32)
    set(ASCO_SOURCES ${ASCO_SOURCES}
        core/os/windows/process.cpp
        core/os/windows/terminal.cpp
    )

    if (ASCO_IOCP)
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )
    else()
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )
    endif()
endif()

if (ASCO_IO_URING)
    set (MDEFS ${MDEFS} ASCO_IO_URING)
endif()
if (ASCO_IOCP)
    set (MDEFS ${MDEFS} ASCO_IOCP)
endif()

if (ASCO_PERF_RECORD)
    set (MDEFS ${MDEFS} ASCO_PERF_RECORD)
endif()

if (ASCO_CORE_TASK__EXECUTION_DOMAIN_NEST_MAX_DEPTH)
    set (MDEFS ${MDEFS} ASCO_CORE_TASK__EXECUTION_DOMAIN_NEST_MAX_DEPTH=${ASCO_CORE_TASK__EXECUTION_DOMAIN_NEST_MAX_DEPTH})
endif()

set(COMPILE_OPTIONS
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        -Wno-interference-size
    )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-zero-length-array
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        -ferror-limit=9999999
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        /utf-8
    )
endif()

add_library(asco_core ${ASCO_SOURCES})
target_include_directories(asco_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(asco_core PUBLIC ${DEPLIBS})
target_compile_definitions(asco_core PUBLIC ${MDEFS})
target_compile_options(asco_core PUBLIC ${COMPILE_OPTIONS})
target_precompile_headers(asco_core PUBLIC ${PRECOMPILE_HEADERS})
add_library(asco::core ALIAS asco_core)

add_library(asco_main asco_main.cpp)
target_link_libraries(asco_main PUBLIC asco_core)
add_library(asco::main ALIAS asco_main)

add_library(asco_base asco_main.cpp)
target_link_libraries(asco_base PUBLIC asco_core)
target_compile_definitions(asco_base PRIVATE __ASCORT__)
add_library(asco::base ALIAS asco_base)

option(ASCO_ENABLE_INSTALL "Enable install() rules for packaging" OFF)
if(ASCO_ENABLE_INSTALL)
    include(GNUInstallDirs)
    # Install headers (public API) under include/
    install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/asco
        FILES_MATCHING
        PATTERN "*.h"
    )

    # Install targets and export them for find_package
    install(TARGETS
        asco_core
        asco_main
        asco_base
        EXPORT ascoTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

# Test library

add_library(asco_test test/test_main.cpp)
target_link_libraries(asco_test PRIVATE asco::core asco::base)
target_compile_definitions(asco_test PUBLIC ASCO_TESTING)
add_library(asco::test ALIAS asco_test)
