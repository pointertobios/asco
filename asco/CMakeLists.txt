# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

set(ASCO_SOURCES
    core/daemon.cpp
    core/runtime.cpp
    core/sched.cpp
    core/timer.cpp
    future.cpp
    futures.cpp
    io/file.cpp
    io/bufio.cpp
    sync/condition_variable.cpp
    unwind/unwind.cpp
)


if (LINUX)
    set(ASCO_SOURCES ${ASCO_SOURCES}
        unwind/linux_unwind.cpp
        io/linux_file.cpp
    )

    if (NOT ASCO_IO_URING)
        set(ASCO_SOURCES ${ASCO_SOURCES}
            core/linux/io_thread.cpp
        )
    else()
        set(ASCO_SOURCES ${ASCO_SOURCES}
            core/linux/io_uring.cpp
            io/uring_file.cpp
        )
    endif()
    
    set(DEPLIBS ${DEPLIBS} dw elf z)

    if (ASCO_IO_URING)
        set(DEPLIBS ${DEPLIBS} uring)
    endif()
endif()

set(MDEFS ASCO __ASCO__)
if (ASCO_PERF_RECORD)
    set (MDEFS ${MDEFS} ASCO_PERF_RECORD)
endif()
if (ASCO_IO_URING)
    set (MDEFS ${MDEFS} ASCO_IO_URING)
endif()
if (ASCO_IOCP)
    set (MDEFS ${MDEFS} ASCO_IOCP)
endif()

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
target_link_libraries(asco PUBLIC ${DEPLIBS})
target_compile_definitions(asco PUBLIC ${MDEFS})

add_library(asco-shared SHARED ${ASCO_SOURCES})
target_include_directories(asco-shared PUBLIC ../)
target_link_libraries(asco-shared PUBLIC ${DEPLIBS})
target_compile_definitions(asco-shared PUBLIC ${MDEFS})
if (NOT WIN32)
    set_target_properties(asco-shared PROPERTIES OUTPUT_NAME asco)
endif()

add_library(asco-main asco_main.cpp)
target_link_libraries(asco-main PUBLIC asco)

add_library(asco-base asco_main.cpp)
target_link_libraries(asco-base PUBLIC asco)
target_compile_definitions(asco-base PRIVATE __ASCORT__)

add_library(asco-main-shared asco_main.cpp)
target_link_libraries(asco-main-shared PUBLIC asco-shared)

add_library(asco-base-shared asco_main.cpp)
target_link_libraries(asco-base-shared PUBLIC asco-shared)
target_compile_definitions(asco-base-shared PRIVATE __ASCORT__)
   