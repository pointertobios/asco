// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_TEMPLATES_H
#define ASCO_UTILS_TEMPLATES_H 1

#include <cstddef>

namespace asco::templates {

template<std::size_t N>
struct literal_string {
    constexpr literal_string(const char (&str)[N]) noexcept {
        for (std::size_t i{0}; i < N; i++) inner[i] = str[i];
    }

    constexpr char operator[](std::size_t i) const noexcept { return inner[i]; }

    constexpr auto size() const noexcept { return N - 1; }

    constexpr operator const char *() const noexcept { return inner; }

    char inner[N];
};

};  // namespace asco::templates

#endif
