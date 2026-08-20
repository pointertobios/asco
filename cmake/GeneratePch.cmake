# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

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

    set(_excludes)
    foreach(_exclude IN LISTS ARG_EXCLUDE)
        if(NOT IS_ABSOLUTE "${_exclude}")
            set(_exclude "${ARG_INCLUDE_DIR}/${_exclude}")
        endif()
        string(REPLACE "\\" "/" _exclude "${_exclude}")
        list(APPEND _excludes "${_exclude}")
    endforeach()

    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
        "${ARG_INCLUDE_DIR}/*.h"
        "${ARG_INCLUDE_DIR}/*.hpp"
    )

    set(_lines)
    foreach(_header IN LISTS _headers)
        string(REPLACE "\\" "/" _header_abs "${_header}")
        if("${_header_abs}" IN_LIST _excludes)
            continue()
        endif()
        file(RELATIVE_PATH _relative "${ARG_INCLUDE_DIR}" "${_header}")
        string(REPLACE "\\" "/" _relative "${_relative}")
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
