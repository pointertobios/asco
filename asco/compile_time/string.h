// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_COMPILE_TIME_STRING_H
#define ASCO_COMPILE_TIME_STRING_H 1

#include <asco/utils/pubusing.h>

namespace asco::compile_time {

template<std::size_t N>
struct string {
    consteval string(const char (&str)[N]) noexcept {
        for (std::size_t i{0}; i < N; i++) inner[i] = str[i];
    }

    consteval char operator[](std::size_t i) const noexcept { return inner[i]; }

    consteval auto size() const noexcept { return N - 1; }

    consteval operator const char *() const noexcept { return inner; }

    char inner[N];
};

};  // namespace asco::compile_time

#endif
