// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

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
concept move_secure = std::movable<T> || std::is_void_v<T> || std::is_pointer_v<T>;

template<typename T, template<typename...> typename Template>
struct is_specialization_of : std::false_type {};

template<template<typename...> typename Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

template<typename T, template<typename...> typename Template>
concept specialization_of = is_specialization_of<T, Template>::value;

template<typename K>
concept hash_key =
    std::equality_comparable<K> && std::copyable<K> && std::is_nothrow_destructible_v<K> && requires(K k) {
        { std::hash<K>{}(k) } -> std::convertible_to<std::size_t>;
    };

};  // namespace asco::util::types
