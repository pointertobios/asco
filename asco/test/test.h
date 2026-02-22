// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef ASCO_TESTING

#    include <expected>
#    include <format>           // IWYU pragma: keep
#    include <source_location>  // IWYU pragma: keep
#    include <variant>

#    include <asco/future.h>

namespace asco::test {

using test_result = std::expected<std::monostate, std::string>;
using test_function = std::function<future<test_result>()>;

bool add_test(std::string name, test_function fn);

};  // namespace asco::test

#    define ASCO_TEST(name)                                                       \
        asco::future<asco::test::test_result> test_##name();                      \
        [[maybe_unused]]                                                          \
        bool test_##name##_registered = asco::test::add_test(#name, test_##name); \
        asco::future<asco::test::test_result> test_##name()

#    define ASCO_CHECK(expr, fmt, ...)                                                                 \
        do {                                                                                           \
            if (!(expr)) {                                                                             \
                auto sl = std::source_location::current();                                             \
                auto hint = std::format(fmt, ##__VA_ARGS__);                                           \
                co_return std::unexpected{                                                             \
                    std::format("{}\n  位于 {}:{}:{}", hint, sl.file_name(), sl.line(), sl.column())}; \
            }                                                                                          \
        } while (false)

#    define ASCO_SUCCESS() co_return std::monostate{};

#endif
