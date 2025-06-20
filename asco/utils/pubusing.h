// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_PUBUSING_H
#define ASCO_UTILS_PUBUSING_H 1

#include <atomic>
#include <concepts>
#include <cstddef>

#ifdef _WIN32
#    define __always_inline __forceinline
#endif

namespace asco::types {

using size_t = uintptr_t;

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

};  // namespace asco::types

namespace asco::literals {

using namespace asco::types;

consteval inline size_t operator""_B(unsigned long long n) { return n; }

consteval inline size_t operator""_KB(unsigned long long n) { return n * 1024; }

consteval inline size_t operator""_MB(unsigned long long n) { return n * 1024 * 1024; }

consteval inline size_t operator""_GB(unsigned long long n) { return n * 1024 * 1024 * 1024; }

consteval inline size_t operator""_TB(unsigned long long n) { return n * 1024 * 1024 * 1024 * 1024; }

};  // namespace asco::literals

#define __dispatch(_1, _2, _3, NAME, ...) NAME

#define with(decl) if (decl; true)

#endif
