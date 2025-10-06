// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <format>

#include <asco/future.h>
#include <asco/io/standard.h>

namespace asco {

template<typename... Args>
future<void> print(std::format_string<Args...> fmt, Args &&...args) {
    co_await stdout().write(std::vformat(fmt.get(), std::make_format_args(args...)));
}

template<typename... Args>
future<void> eprint(std::format_string<Args...> fmt, Args &&...args) {
    co_await stderr().write(std::vformat(fmt.get(), std::make_format_args(args...)));
}

template<typename... Args>
future<void> println(std::format_string<Args...> fmt, Args &&...args) {
    buffer<> buf{std::vformat(fmt.get(), std::make_format_args(args...)) + '\n'};
    co_await stdout().write(std::move(buf));
}

template<typename... Args>
future<void> eprintln(std::format_string<Args...> fmt, Args &&...args) {
    buffer<> buf{std::vformat(fmt.get(), std::make_format_args(args...)) + '\n'};
    co_await stderr().write(std::move(buf));
}

};  // namespace asco
