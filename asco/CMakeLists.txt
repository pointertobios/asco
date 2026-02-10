# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

set(ASCO_SOURCES
    core/daemon.cpp
    core/runtime.cpp
    core/worker.cpp
)

set(PRECOMPILE_HEADERS
    concurrency/concurrency.h
    concurrency/hash_map.h
    concurrency/ring_queue.h
    core/daemon.h
    core/runtime.h
    core/worker.h
    future.h
    invoke.h
    join_handle.h
    panic.h
    util/consts.h
    util/erased.h
    util/raw_storage.h
    util/types.h
)

set(MDEFS ASCO __ASCO__)
set(DEPLIBS)

if (USING_LIBCXX)
    set(MDEFS USING_LIBCXX)
endif()

if (LINUX)
    if (NOT ASCO_IO_URING)
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )
    else()
        set(ASCO_SOURCES ${ASCO_SOURCES}
        )
    endif()

    if (ASCO_IO_URING)
        set(DEPLIBS ${DEPLIBS} uring)
    endif()
endif()

if (ASCO_PERF_RECORD)
    set (MDEFS ${MDEFS} ASCO_PERF_RECORD)
endif()
if (ASCO_IO_URING)
    set (MDEFS ${MDEFS} ASCO_IO_URING)
endif()
if (ASCO_IOCP)
    set (MDEFS ${MDEFS} ASCO_IOCP)
endif()

set(COMPILE_OPTIONS
    -Wall
    -Wextra
    -Weverything
    -Wno-c++98-compat
    -Wno-c++98-compat-extra-semi
    -Wno-missing-prototypes
    -Wno-padded
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS ${COMPILE_OPTIONS}
        -Wno-interference-size
    )
endif()

add_library(asco_core ${ASCO_SOURCES})
target_include_directories(asco_core PUBLIC
    # NOTE: when used via FetchContent/add_subdirectory, CMAKE_SOURCE_DIR points to the parent project.
    # PROJECT_SOURCE_DIR always points to this project root (the directory containing the `asco/` folder).
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(asco_core PUBLIC ${DEPLIBS})
target_compile_definitions(asco_core PUBLIC ${MDEFS})
target_compile_options(asco_core PUBLIC ${COMPILE_OPTIONS})
target_precompile_headers(asco_core PUBLIC ${PRECOMPILE_HEADERS})
add_library(asco::core ALIAS asco_core)

add_library(asco_shared_core SHARED ${ASCO_SOURCES})
target_include_directories(asco_shared_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(asco_shared_core PUBLIC ${DEPLIBS})
target_compile_definitions(asco_shared_core PUBLIC ${MDEFS})
target_compile_options(asco_shared_core PUBLIC ${COMPILE_OPTIONS})
target_precompile_headers(asco_shared_core PUBLIC ${PRECOMPILE_HEADERS})
if (NOT WIN32)
    set_target_properties(asco_shared_core PROPERTIES OUTPUT_NAME asco_core)
endif()
add_library(asco::shared::core ALIAS asco_shared_core)

add_library(asco_main asco_main.cpp)
target_link_libraries(asco_main PUBLIC asco_core)
add_library(asco::main ALIAS asco_main)

add_library(asco_base asco_main.cpp)
target_link_libraries(asco_base PUBLIC asco_core)
target_compile_definitions(asco_base PRIVATE __ASCORT__)
add_library(asco::base ALIAS asco_base)

add_library(asco_shared_main SHARED asco_main.cpp)
target_link_libraries(asco_shared_main PUBLIC asco_shared_core)
add_library(asco::shared::main ALIAS asco_shared_main)

add_library(asco_shared_base SHARED asco_main.cpp)
target_link_libraries(asco_shared_base PUBLIC asco_shared_core)
target_compile_definitions(asco_shared_base PRIVATE __ASCORT__)
add_library(asco::shared::base ALIAS asco_shared_base)

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
        asco_shared_core
        asco_main
        asco_base
        asco_shared_main
        asco_shared_base
        EXPORT ascoTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
