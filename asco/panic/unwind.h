// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include <cpptrace/basic.hpp>

namespace asco::panic {

using coroutine_trace_handle = cpptrace::stacktrace_frame;

std::string unwind(size_t skip = 0, bool color = true);

std::string co_unwind(size_t skip = 0, bool color = true);

};  // namespace asco::panic
