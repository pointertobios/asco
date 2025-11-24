// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

#include <asco/panic/panic.h>

namespace asco {

[[noreturn]] void assert_failed(std::string_view expr);

[[noreturn]] void assert_failed(std::string_view expr, std::string_view hint);

};  // namespace asco

#define asco_assert(expr, ...)                         \
    do {                                               \
        if (!(expr)) {                                 \
            asco::assert_failed(#expr, ##__VA_ARGS__); \
        }                                              \
    } while (0)
