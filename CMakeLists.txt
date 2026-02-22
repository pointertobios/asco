# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(asco VERSION 0.1.0 LANGUAGES CXX)

# Compiler and linker config

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(ProcessorCount)

ProcessorCount(CPUS)

check_cxx_compiler_flag("-stdlib=libc++" COMPILER_SUPPORTS_LIBCXX)

if (COMPILER_SUPPORTS_LIBCXX)
    set(_save_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(_save_required_includes "${CMAKE_REQUIRED_INCLUDES}")
    set(_save_required_libs "${CMAKE_REQUIRED_LIBRARIES}")

    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -stdlib=libc++")
    if (LIBCXX_INCLUDE_DIR)
        set(CMAKE_REQUIRED_INCLUDES "${LIBCXX_INCLUDE_DIR}")
    endif()
    if (LIBCXX_LIB)
        list(APPEND CMAKE_REQUIRED_LIBRARIES "${LIBCXX_LIB}")
    endif()
    if (LIBCXXABI_LIB)
        list(APPEND CMAKE_REQUIRED_LIBRARIES "${LIBCXXABI_LIB}")
    endif()

    check_cxx_source_compiles("
        #include <string>
        #include <iostream>
        int main(){ std::string s = \"ok\"; std::cout << s; return 0; }
    " LIBCXX_COMPILES)

    set(CMAKE_REQUIRED_FLAGS "${_save_required_flags}")
    set(CMAKE_REQUIRED_INCLUDES "${_save_required_includes}")
    set(CMAKE_REQUIRED_LIBRARIES "${_save_required_libs}")
else()
    set(LIBCXX_COMPILES OFF)
endif()

set(USING_LIBCXX OFF)

if (LIBCXX_COMPILES)
    set(USING_LIBCXX ON)
    message(STATUS "C++ stdlib: LLVM libc++")
    set(CMAKE_REQUIRED_FLAGS "-stdlib=libc++")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
else()
    message(STATUS "C++ stdlib: GNU libstdc++")
    message(STATUS "LLVM libc++ is recommended")
endif()

if (NOT WIN32)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
# add_link_options(-fsanitize=address -fno-omit-frame-pointer)

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

# Dependencies


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
add_subdirectory(examples)
add_subdirectory(tests)

# Packaging for build-tree find_package

include(CMakePackageConfigHelpers)

export(
    TARGETS
        asco_core
        asco_shared_core
        asco_main
        asco_base
        asco_shared_main
        asco_shared_base
    NAMESPACE asco::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/ascoTargets.cmake"
)

# Generate build-tree package config files
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ascoConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ascoConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ascoConfig.cmake"
    INSTALL_DESTINATION lib/cmake/asco
)

# Register build tree in the User Package Registry so consumers can find it via find_package(asco)
export(PACKAGE asco)

# Install rules for packaging (when ASCO_ENABLE_INSTALL is ON)
if(ASCO_ENABLE_INSTALL)
    include(GNUInstallDirs)
    
    # Install export targets
    install(
        EXPORT ascoTargets
        NAMESPACE asco::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asco
    )
    
    # Install package config files
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/ascoConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/ascoConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asco
    )
endif()
