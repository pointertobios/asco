// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

namespace asco::types {

template<typename T, template<typename...> typename Template>
struct is_specialization_of : std::false_type {};

template<template<typename...> typename Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

template<typename T, template<typename...> typename Template>
concept specialization_of = is_specialization_of<T, Template>::value;

};  // namespace asco::types
