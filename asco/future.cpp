// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#define FUTURE_IMPL
#include <asco/future.h>

#include <asco/core/runtime.h>

#include <string>
#include <string_view>

namespace asco::base {

template struct future_base<void, false, false>;
template struct future_base<void, true, false>;
template struct future_base<void, false, true>;

template struct future_base<int, false, false>;
template struct future_base<int, true, false>;
template struct future_base<int, false, true>;

template struct future_base<std::string, false, false>;
template struct future_base<std::string, true, false>;
template struct future_base<std::string, false, true>;

template struct future_base<std::string_view, false, false>;
template struct future_base<std::string_view, true, false>;
template struct future_base<std::string_view, false, true>;

template struct future_base<io::buffer<>, false, false>;
template struct future_base<io::buffer<>, true, false>;
template struct future_base<io::buffer<>, false, true>;

template struct future_base<std::optional<io::buffer<>>, false, false>;
template struct future_base<std::optional<io::buffer<>>, true, false>;
template struct future_base<std::optional<io::buffer<>>, false, true>;

};  // namespace asco::base
