# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

# asco_collect_files(<out_var>
#     INCLUDE_DIR <dir>
#     PATTERNS <glob>...
#     [EXCLUDE <path>...]
# )
#
# Recursively collects every file below INCLUDE_DIR matching at least one of
# PATTERNS (with CONFIGURE_DEPENDS), skipping anything covered by EXCLUDE.
#
# EXCLUDE entries may be given as absolute paths or as paths relative to
# INCLUDE_DIR; a directory entry excludes the directory itself and everything
# below it. Matching is component-aware, so excluding "asco/test" does not
# affect a sibling named "asco/testing".
#
# The absolute paths of the remaining files are returned through <out_var> in
# the order produced by the glob (callers that need a stable order sort the
# result themselves).
function(asco_collect_files out_var)
    set(options)
    set(oneValueArgs INCLUDE_DIR)
    set(multiValueArgs PATTERNS EXCLUDE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_INCLUDE_DIR)
        message(FATAL_ERROR "asco_collect_files: INCLUDE_DIR is required")
    endif()
    if(NOT ARG_PATTERNS)
        message(FATAL_ERROR "asco_collect_files: PATTERNS is required")
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

    set(_globs)
    foreach(_pattern IN LISTS ARG_PATTERNS)
        list(APPEND _globs "${_include_dir_abs}/${_pattern}")
    endforeach()

    file(GLOB_RECURSE _candidates CONFIGURE_DEPENDS ${_globs})

    set(_files)
    foreach(_candidate IN LISTS _candidates)
        file(TO_CMAKE_PATH "${_candidate}" _candidate_abs)
        set(_excluded OFF)
        foreach(_exclude IN LISTS _excludes)
            if(_candidate_abs STREQUAL _exclude)
                set(_excluded ON)
                break()
            endif()

            string(LENGTH "${_exclude}" _exclude_length)
            string(LENGTH "${_candidate_abs}" _candidate_length)
            if(_exclude_length GREATER _candidate_length)
                continue()
            endif()
            string(SUBSTRING "${_candidate_abs}" 0 ${_exclude_length}
                _candidate_prefix)
            if(_candidate_prefix STREQUAL _exclude)
                string(SUBSTRING "${_candidate_abs}" ${_exclude_length} -1
                    _candidate_suffix)
                if(_candidate_suffix MATCHES "^/")
                    set(_excluded ON)
                    break()
                endif()
            endif()
        endforeach()
        if(_excluded)
            continue()
        endif()
        list(APPEND _files "${_candidate_abs}")
    endforeach()

    set(${out_var} "${_files}" PARENT_SCOPE)
endfunction()
