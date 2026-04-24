// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

namespace asco::util::compile_config {

inline constexpr bool perf_record =
#ifdef ASCO_PERF_RECORD
    true;
#else
    false;
#endif

namespace core::task {

inline constexpr std::size_t execution_domain_nest_max_depth =
#ifdef ASCO_CORE_TASK__EXECUTION_DOMAIN_NEST_MAX_DEPTH
    ASCO_CORE_TASK__EXECUTION_DOMAIN_NEST_MAX_DEPTH;
#else
    4;
#endif

};  // namespace core::task

};  // namespace asco::util::compile_config
