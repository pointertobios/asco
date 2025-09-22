// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_COMPILE_TIME_STRING_H
#define ASCO_COMPILE_TIME_STRING_H 1

#include <tuple>

#include <asco/utils/pubusing.h>

namespace asco::compile_time {

template<size_t N>
struct string {
    consteval string(const char (&str)[N]) noexcept {
        for (size_t i{0}; i < N - 1; i++) inner[i] = str[i];
        inner[N - 1] = '\0';
    }

    consteval char operator[](size_t i) const noexcept { return inner[i]; }

    consteval auto size() const noexcept { return N - 1; }

    consteval operator const char *() const noexcept { return inner; }

    consteval size_t hash() const noexcept {
        constexpr size_t offset_basis = 0xcbf29ce484222325ULL;
        constexpr size_t prime = 0x100000001b3ULL;
        size_t hash = offset_basis;
        for (const char *p = inner; *p != '\0'; ++p) {
            hash ^= static_cast<size_t>(*p);
            hash *= prime;
        }
        return hash;
    }

    char inner[N];
};

};  // namespace asco::compile_time

#endif
