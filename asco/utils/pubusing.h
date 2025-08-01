// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_PUBUSING_H
#define ASCO_UTILS_PUBUSING_H 1

#include <atomic>
#include <chrono>
#include <cstddef>

#ifdef __linux__
#    define __asco_always_inline __inline __attribute__((__always_inline__))
#elifdef _WIN32
#    define __asco_always_inline __forceinline
#endif

namespace asco::types {

using size_t = uintptr_t;
using ssize_t = intptr_t;

using atomic_size_t = std::atomic<size_t>;
using atomic_int64_t = std::atomic_int64_t;
using atomic_bool = std::atomic_bool;
using atomic_flag = std::atomic_flag;

using offset_t = std::ptrdiff_t;

template<typename T>
using atomic = std::atomic<T>;

using morder = std::memory_order;

};  // namespace asco::types

namespace asco::literals {

using namespace types;

consteval inline size_t operator""_B(unsigned long long n) { return n; }

consteval inline size_t operator""_KB(unsigned long long n) { return n * 1024; }

consteval inline size_t operator""_MB(unsigned long long n) { return n * 1024 * 1024; }

consteval inline size_t operator""_GB(unsigned long long n) { return n * 1024 * 1024 * 1024; }

consteval inline size_t operator""_TB(unsigned long long n) { return n * 1024 * 1024 * 1024 * 1024; }

};  // namespace asco::literals

namespace std::this_thread {

#ifdef __linux__

template<typename _Rep, typename _Period>
inline void interruptable_sleep_for(const chrono::duration<_Rep, _Period> &__rtime) {
    if (__rtime <= __rtime.zero())
        return;
    auto __s = chrono::duration_cast<chrono::seconds>(__rtime);
    auto __ns = chrono::duration_cast<chrono::nanoseconds>(__rtime - __s);
    struct ::timespec __ts = {static_cast<std::time_t>(__s.count()), static_cast<long>(__ns.count())};
    ::nanosleep(&__ts, &__ts);
}

#elif
#    error "Not implemented on this platform"
#endif

};  // namespace std::this_thread

#define __dispatch(_1, _2, _3, NAME, ...) NAME

#define with(decl) if (decl; true)

#endif
