// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <type_traits>

namespace asco::concepts {

template<typename T>
concept move_secure =
    std::is_void_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_pointer_v<T>
    || (std::is_move_constructible_v<std::conditional<std::is_void_v<T>, std::monostate, T>>
        && std::is_move_assignable_v<std::conditional<std::is_void_v<T>, std::monostate, T>>);

template<typename T>
concept base_type =
    std::is_void_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_pointer_v<T>;

template<typename T>
concept is_void = std::is_void_v<T>;

template<typename T>
concept non_void = !std::is_void_v<T>;

template<typename Ti>
concept duration_type = std::is_same_v<Ti, std::chrono::duration<typename Ti::rep, typename Ti::period>>;

template<typename Tp>
concept time_point_type =
    std::is_same_v<Tp, std::chrono::time_point<typename Tp::clock, typename Tp::duration>>;

};  // namespace asco::concepts
