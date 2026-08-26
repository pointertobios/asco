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

    get_filename_component(_include_dir_abs "${ARG_INCLUDE_DIR}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_include_dir_abs}" _include_dir_abs)

    set(_excludes)
    foreach(_exclude IN LISTS ARG_EXCLUDE)
        get_filename_component(_exclude_abs "${_exclude}" ABSOLUTE
            BASE_DIR "${_include_dir_abs}")
        file(TO_CMAKE_PATH "${_exclude_abs}" _exclude_abs)
        list(APPEND _excludes "${_exclude_abs}")
    endforeach()

    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
        "${_include_dir_abs}/*.h"
        "${_include_dir_abs}/*.hpp"
    )

    set(_lines)
    foreach(_header IN LISTS _headers)
        file(TO_CMAKE_PATH "${_header}" _header_abs)
        set(_excluded OFF)
        foreach(_exclude IN LISTS _excludes)
            if(_header_abs STREQUAL _exclude)
                set(_excluded ON)
                break()
            endif()

            string(LENGTH "${_exclude}" _exclude_length)
            string(LENGTH "${_header_abs}" _header_length)
            if(_exclude_length GREATER _header_length)
                continue()
            endif()
            string(SUBSTRING "${_header_abs}" 0 ${_exclude_length}
                _header_prefix)
            if(_header_prefix STREQUAL _exclude)
                string(SUBSTRING "${_header_abs}" ${_exclude_length} -1
                    _header_suffix)
                if(_header_suffix MATCHES "^/")
                    set(_excluded ON)
                    break()
                endif()
            endif()
        endforeach()
        if(_excluded)
            continue()
        endif()
        file(RELATIVE_PATH _relative "${_include_dir_abs}" "${_header_abs}")
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
