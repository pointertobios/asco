// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <asco/future.h>

inline asco::runtime_initializer_t runtime_initializer = [] () {
    std::cout << "runtime_initializer test" << std::endl;
    return new asco::runtime;
};

using asco::future, asco::future_void_inline;

future<std::string> bar() {
    std::cout << "bar" << std::endl;
    co_return "Hello, World! from bar";
}

future_void_inline foo() {
    auto s = co_await bar();
    std::cout << "foo got: " << s << std::endl;
    co_return {};
}

future<int> async_main() {
    std::cout << "Hello, World!" << std::endl;
    co_await foo();
    std::cout << "main done" << std::endl;
    co_return 0;
}
