// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <type_traits>

#include <asco/utils/types.h>

namespace asco::concepts {

using namespace types;

template<typename T>
concept move_secure =
    std::is_void_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_pointer_v<T>
    || (std::is_move_constructible_v<monostate_if_void<T>>
        && std::is_move_assignable_v<monostate_if_void<T>>);

template<typename T>
concept base_type =
    std::is_void_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_pointer_v<T>;

template<typename T>
concept is_void = std::is_void_v<T>;

template<typename T>
concept non_void = !std::is_void_v<T>;

template<typename T>
using passing = std::conditional_t<base_type<T>, T, T &&>;

template<typename Ti>
concept duration_type = std::is_same_v<Ti, std::chrono::duration<typename Ti::rep, typename Ti::period>>;

};  // namespace asco::concepts
