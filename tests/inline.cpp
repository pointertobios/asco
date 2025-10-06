// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/print.h>

inline asco::runtime_initializer_t runtime_initializer = []() {
    asco::println("runtime_initializer test");
    return new asco::core::runtime;
};

using asco::future, asco::future_inline;

future<std::string> bar() {
    co_await asco::println("bar");
    co_return "Hello, World! from bar";
}

future_inline<void> foo() {
    auto s = co_await bar();
    co_await asco::println("foo got: {}", s);
    co_return;
}

future<int> async_main() {
    co_await asco::println("Hello, World! ");
    co_await foo();
    co_await asco::println("main done");
    co_return 0;
}
