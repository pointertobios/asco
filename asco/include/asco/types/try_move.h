// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

namespace asco::types {

template<typename T>
using try_move_t = std::conditional_t<
    std::is_move_constructible_v<T> && !std::is_fundamental_v<T>, T &&,
    std::conditional_t<std::is_copy_constructible_v<T> && !std::is_fundamental_v<T>, const T &, T>>;

template<typename T>
inline try_move_t<std::remove_cvref_t<T>> try_move(T &&t) {
    if constexpr (std::is_move_constructible_v<T> && !std::is_fundamental_v<T>) {
        return std::move(t);
    } else if constexpr (std::is_copy_constructible_v<T> && !std::is_fundamental_v<T>) {
        return t;
    } else {
        return t;
    }
}

template<typename T>
inline constexpr bool is_nothrow_try_movable_v =
    std::is_fundamental_v<T> || std::is_nothrow_copy_constructible_v<T>
    || std::is_nothrow_move_constructible_v<T>;

};  // namespace asco::types

namespace asco {

using types::try_move;
using types::try_move_t;

};  // namespace asco
