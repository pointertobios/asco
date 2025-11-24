// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <format>
#include <functional>
#include <string>

#include <cpptrace/basic.hpp>

namespace asco::panic {

[[noreturn]] void panic(std::string msg) noexcept;

[[noreturn]] void co_panic(std::string msg) noexcept;

template<typename... Args>
[[noreturn]] void panic(std::format_string<Args...> fmt, Args &&...args) {
    panic(std::vformat(fmt.get(), std::make_format_args(args...)));
}

template<typename... Args>
[[noreturn]] void co_panic(std::format_string<Args...> fmt, Args &&...args) {
    co_panic(std::vformat(fmt.get(), std::make_format_args(args...)));
}

void register_callback(std::function<void(cpptrace::stacktrace &, std::string_view)> cb);

};  // namespace asco::panic
