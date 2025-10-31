// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/panic/panic.h>

#include <cstdlib>
#include <print>

#include <asco/panic/color.h>
#include <asco/panic/unwind.h>

namespace asco::panic {

[[noreturn]] void panic(std::string msg) noexcept {
    std::println(
        stderr, "{}[ASCO] {}{}\nStack trace(most recent call last):\n{}", panic_color, msg, reset_color,
        unwind(2));
    std::abort();
}

[[noreturn]] void co_panic(std::string msg) noexcept {
    std::println(
        stderr, "{}[ASCO] {}{}\nStack and await chain trace(most recent call/await last):\n{}", panic_color,
        msg, reset_color, co_unwind(2));
    std::abort();
}

};  // namespace asco::panic
