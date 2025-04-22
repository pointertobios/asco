// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UTILS_PUBUSING_H
#define ASCO_UTILS_PUBUSING_H 1

#include <cstddef>
#include <atomic>

namespace asco {

using size_t = std::size_t;

using atomic_size_t = std::atomic_size_t;
using atomic_in64_t = std::atomic_int64_t;
using atomic_bool = std::atomic_bool;

template<typename T>
using atomic = std::atomic<T>;

using morder = std::memory_order;

#ifdef _WIN32
    using __u8 = unsigned char;
#endif

};

#ifdef _WIN32
    #define __always_inline __forceinline
#endif

#endif
