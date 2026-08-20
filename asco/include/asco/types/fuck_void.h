// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>
#include <variant>

#include "asco/concepts.h"

namespace asco::types {

template<typename T>
using fuck_void = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

template<typename T, concepts::non_void U>
using use_or_fuck_void = std::conditional_t<std::is_void_v<T>, U, T>;

template<typename T, concepts::non_void U, concepts::non_void V>
using fuck_void_or_else = std::conditional_t<std::is_void_v<T>, V, U>;

};  // namespace asco::types
