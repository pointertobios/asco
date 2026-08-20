// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/panic.h"

namespace asco {

#ifdef ASCO_DEBUG_ENABLED
#    define ASCO_ASSERT(expr, ...)                                  \
        do {                                                        \
            if (!(expr)) {                                          \
                std::string lint;                                   \
                __VA_OPT__(lint = ": " + std::format(__VA_ARGS__);) \
                panic("'{}' assertion failed{}", #expr, lint);      \
            }                                                       \
        } while (false)
#else
#    define ASCO_ASSERT(expr, ...) ((void)(expr))
#endif

};  // namespace asco
