// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UTILS_H
#define ASCO_UTILS_H

#include <concepts>

namespace asco {

template<typename T>
constexpr bool is_move_secure_v = 
    (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
        || std::is_integral_v<T> || std::is_floating_point_v<T>
        || std::is_pointer_v<T> || std::is_void_v<T>;

};

#endif
