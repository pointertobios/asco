// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/util/types.h>

#if defined(_MSC_VER)
#    if defined(_M_IX86) || defined(_M_X64)
#        include <immintrin.h>  // _mm_pause
#    elif defined(_M_ARM) || defined(_M_ARM64)
#        include <intrin.h>  // __yield
#    endif
#endif

namespace asco::concurrency {

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#    if defined(_MSC_VER)
    _mm_pause();
#    elif defined(__GNUC__) || defined(__clang__)
    __builtin_ia32_pause();
#    else
    asm volatile("pause");
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
#    if defined(_MSC_VER)
    __yield();
#    elif defined(__GNUC__) || defined(__clang__)
    asm volatile("yield");
#    endif
#endif
}

template<std::size_t N>
void withdraw() noexcept {
    for (std::size_t i{0}; i < N; ++i)
        concurrency::cpu_relax();
}

inline void exp_withdraw(std::size_t i) noexcept {
    switch (1 << i) {
    case 1:
        withdraw<1>();
        break;
    case 2:
        withdraw<2>();
        break;
    case 4:
        withdraw<4>();
        break;
    case 8:
        withdraw<8>();
        break;
    case 16:
        withdraw<16>();
        break;
    case 32:
        withdraw<32>();
        break;
    case 64:
    default:
        withdraw<64>();
        break;
    }
}

};  // namespace asco::concurrency
