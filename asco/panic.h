// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <exception>
#include <format>
#include <print>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace asco {

namespace detail {

template<typename... Args>
class format_string_with_location_type {
public:
    template<typename T>
        requires(std::convertible_to<const T &, std::string_view>)
    consteval format_string_with_location_type(
        const T &fmt, std::source_location sl = std::source_location::current())
            : m_fmt{fmt}
            , m_sl{sl} {}

    constexpr auto get() const { return m_fmt.get(); }

    constexpr auto location() const { return m_sl; }

private:
    std::format_string<Args...> m_fmt;
    std::source_location m_sl;
};

template<typename... Args>
using format_string_with_location = format_string_with_location_type<std::type_identity_t<Args>...>;

};  // namespace detail

#ifdef ASCO_TESTING
class panicked : std::exception {
public:
    panicked(std::string msg, std::string loc)
            : m_msg{std::move(msg)}
            , m_loc{std::move(loc)} {}

    std::string to_string() { return std::format("{}\n  {}", m_msg, m_loc); }

private:
    std::string m_msg;
    std::string m_loc;
};
#endif

template<typename... Args>
[[noreturn]] void panic(detail::format_string_with_location<Args...> fmt, Args &&...args) {
    auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
    auto loc = std::format(
        "at {}:{}:{}", fmt.location().file_name(), fmt.location().line(),
        fmt.location().column() - 6);  // 这个 -6 是要把位置定在 panic( 的开头
#ifdef ASCO_TESTING
    throw panicked{msg, loc};
#endif
    std::println("[ASCO] \033[1;31mpanic\033[0m: {}\n  {}", msg, loc);
    std::terminate();
}

};  // namespace asco

#ifndef NDEBUG

#    define asco_assert(expr)                               \
        do {                                                \
            if (!(expr)) [[unlikely]] {                     \
                asco::panic("表达式 '{}' 断言失败", #expr); \
            }                                               \
        } while (0)

#    define asco_assert_lint(expr, fmt, ...)                           \
        do {                                                           \
            if (!(expr)) [[unlikely]] {                                \
                auto msg = std::format(fmt, ##__VA_ARGS__);            \
                asco::panic("表达式 '{}' 断言失败\n  {}", #expr, msg); \
            }                                                          \
        } while (0)

#else

#    define asco_assert(expr) ((void)0)
#    define asco_assert_lint(expr, fmt, ...) ((void)0)

#endif
