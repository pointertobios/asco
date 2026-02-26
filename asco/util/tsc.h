// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>  // IWYU pragma: keep
#include <cstdint>

namespace asco::util {

inline std::uint64_t get_tsc() noexcept {
#ifdef __x86_64__
    return __rdtsc();
#elifdef __aarch64__
    std::uint64_t cntvct_el0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(cntvct_el0));
    return cntvct_el0;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

};  // namespace asco::util
