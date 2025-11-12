// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <variant>

#include <asco/utils/concepts.h>

namespace asco::types {

using uint8_t = std::uint8_t;
using size_t = std::size_t;
using uint128_t = __uint128_t;

using morder = std::memory_order;

template<typename T>
using atomic = std::atomic<T>;

using atomic_bool = std::atomic_bool;
using atomic_uint8_t = std::atomic_uint8_t;
using atomic_size_t = std::atomic_size_t;

template<typename T>
using monostate_if_void = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

struct type_erase {};

template<typename T>
using passing = std::conditional_t<concepts::base_type<T>, T, T &&>;

};  // namespace asco::types
