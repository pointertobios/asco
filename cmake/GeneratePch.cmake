# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

include(${CMAKE_CURRENT_LIST_DIR}/CollectFiles.cmake)

function(asco_generate_pch out_var)
    set(options)
    set(oneValueArgs INCLUDE_DIR OUTPUT)
    set(multiValueArgs EXCLUDE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_INCLUDE_DIR)
        message(FATAL_ERROR "asco_generate_pch: INCLUDE_DIR is required")
    endif()
    if(NOT ARG_OUTPUT)
        message(FATAL_ERROR "asco_generate_pch: OUTPUT is required")
    endif()

    get_filename_component(_include_dir_abs "${ARG_INCLUDE_DIR}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_include_dir_abs}" _include_dir_abs)

    asco_collect_files(_headers
        INCLUDE_DIR "${_include_dir_abs}"
        PATTERNS "*.h" "*.hpp"
        EXCLUDE ${ARG_EXCLUDE}
    )

    set(_lines)
    foreach(_header IN LISTS _headers)
        file(RELATIVE_PATH _relative "${_include_dir_abs}" "${_header}")
        file(TO_CMAKE_PATH "${_relative}" _relative)
        list(APPEND _lines "#include <${_relative}>")
    endforeach()

    list(SORT _lines)

    set(_content "#pragma once\n\n")
    foreach(_line IN LISTS _lines)
        string(APPEND _content "${_line}\n")
    endforeach()

    set(_needs_write OFF)
    if(EXISTS "${ARG_OUTPUT}")
        file(READ "${ARG_OUTPUT}" _existing)
        if(NOT _existing STREQUAL _content)
            set(_needs_write ON)
        endif()
    else()
        set(_needs_write ON)
    endif()

    if(_needs_write)
        file(WRITE "${ARG_OUTPUT}" "${_content}")
    endif()

    set(${out_var} "${ARG_OUTPUT}" PARENT_SCOPE)
endfunction()
