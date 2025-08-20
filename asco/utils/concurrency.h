// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_CONCURRENCY_H
#define ASCO_UTILS_CONCURRENCY_H 1

#include <asco/utils/pubusing.h>

#if defined(_MSC_VER)
#    if defined(_M_IX86) || defined(_M_X64)
#        include <immintrin.h>  // _mm_pause
#    elif defined(_M_ARM) || defined(_M_ARM64)
#        include <intrin.h>  // __yield
#    endif
#endif

namespace asco::concurrency {

using namespace types;

__asco_always_inline void cpu_relax() noexcept {
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

template<size_t N>
__asco_always_inline void withdraw() noexcept {
    for (size_t i{0}; i < N; ++i) concurrency::cpu_relax();
}

};  // namespace asco::concurrency

#endif
