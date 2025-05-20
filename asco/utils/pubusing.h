// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UTILS_PUBUSING_H
#define ASCO_UTILS_PUBUSING_H 1

#include <atomic>
#include <concepts>
#include <cstddef>

#ifdef _WIN32
#    define __always_inline __forceinline
#endif

namespace asco {

using size_t = unsigned long long;

using atomic_size_t = std::atomic<size_t>;
using atomic_int64_t = std::atomic_int64_t;
using atomic_bool = std::atomic_bool;

template<typename T>
using atomic = std::atomic<T>;

using morder = std::memory_order;

#ifdef _WIN32
using __u8 = unsigned char;
#endif

template<typename T>
__always_inline void blackbox(T) noexcept {}

template<typename T>
    requires(!(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>))
__always_inline void blackbox(T &) noexcept {}

};  // namespace asco

#define __dispatch(_1, _2, _3, NAME, ...) NAME

#define with(decl) if (decl; true)

#endif
