// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

namespace asco::concepts {

template<typename T>
concept is_void = std::is_void_v<T>;

template<typename T>
concept non_void = !std::is_void_v<T>;

template<typename T>
concept is_enum = std::is_enum_v<T>;

template<typename Fn, typename Ret, typename... Args>
concept verified_invocable = std::is_invocable_r_v<Ret, Fn, Args...>;

};
