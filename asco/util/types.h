// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <variant>

namespace asco::util::types {

template<typename T>
using monostate_if_void = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

template<typename T>
using void_if_monostate = std::conditional_t<std::is_same_v<T, std::monostate>, void, T>;

template<typename T>
using copy_small_or_move =
    std::conditional_t<(sizeof(T) <= 4 * sizeof(void *)) && std::is_trivially_copyable_v<T>, T, T &&>;

template<typename T>
concept move_secure = std::movable<T> || std::is_void_v<T> || std::is_pointer_v<T>;

template<typename T, template<typename...> typename Template>
struct is_specialization_of : std::false_type {};

template<template<typename...> typename Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

template<typename T, template<typename...> typename Template>
concept specialization_of = is_specialization_of<T, Template>::value;

template<typename K>
concept hash_key = std::is_nothrow_copy_assignable_v<K> && std::is_nothrow_copy_constructible_v<K>
                   && std::is_nothrow_destructible_v<K> && std::equality_comparable<K> && std::copyable<K>
                   && std::is_nothrow_destructible_v<K> && requires(K k) {
                          { std::hash<K>{}(k) } -> std::convertible_to<std::size_t>;
                      };

template<typename Ti>
concept duration_type = specialization_of<std::remove_cvref_t<Ti>, std::chrono::duration>;

template<typename Tp>
concept time_point_type = specialization_of<std::remove_cvref_t<Tp>, std::chrono::time_point>;

};  // namespace asco::util::types
