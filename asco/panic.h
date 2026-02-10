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

class panic_testing : std::exception {};

};  // namespace detail

template<typename... Args>
[[noreturn]] void panic(detail::format_string_with_location<Args...> fmt, Args &&...args) {
#ifdef ASCO_TESTING
    // 用于在测试直接抛出异常，测试框架可以捕获这个异常并标记测试成功/失败
    // 可以支持测试编写导致 panic 的行为来测试 panic 的正确性
    throw detail::panic_testing{};
#endif
    auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
    auto loc = std::format(
        "at {}:{}:{}", fmt.location().file_name(), fmt.location().line(),
        fmt.location().column() - 6);  // 这个 -6 是要把位置定在 panic( 的开头
    std::println("[ASCO] \033[1;31mpanic\033[0m: {}\n  {}", msg, loc);
    std::terminate();
}

};  // namespace asco

#define asco_assert(expr)                               \
    do {                                                \
        if (!(expr)) [[unlikely]] {                     \
            asco::panic("表达式 '{}' 断言失败", #expr); \
        }                                               \
    } while (0)

#define asco_assert_lint(expr, fmt, ...)                           \
    do {                                                           \
        if (!(expr)) [[unlikely]] {                                \
            auto msg = std::format(fmt __VA_ARGS__);               \
            asco::panic("表达式 '{}' 断言失败\n  {}", #expr, msg); \
        }                                                          \
    } while (0)
