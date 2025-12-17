// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/panic/panic.h>

#include <cstdlib>
#include <print>

#include <asco/panic/color.h>
#include <asco/panic/unwind.h>

namespace asco::panic {

std::vector<std::function<void(cpptrace::stacktrace &, std::string_view)>> panic_callbacks;

[[noreturn]] void panic(std::string msg) noexcept {
    auto formatted_msg = std::format(
        "{}[ASCO] {}{}\nStack trace(most recent call last):\n{}", panic_color, msg, reset_color, unwind(2));
    auto st = cpptrace::stacktrace::current(1);
    for (auto &cb : panic_callbacks) { cb(st, msg); }
    std::println(stderr, "{}", formatted_msg);
    std::abort();
}

[[noreturn]] void co_panic(std::string msg) noexcept {
    auto formatted_msg = std::format(
        "{}[ASCO] {}{}\nStack and await chain trace(most recent call/await last):\n{}", panic_color, msg,
        reset_color, co_unwind(2));
    auto st = co_stacktrace(2);
    for (auto &cb : panic_callbacks) { cb(st, msg); }
    std::println(stderr, "{}", formatted_msg);
    std::abort();
}

void register_callback(std::function<void(cpptrace::stacktrace &, std::string_view)> cb) {
    panic_callbacks.push_back(std::move(cb));
}

};  // namespace asco::panic
