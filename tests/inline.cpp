// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/future.h>
#include <iostream>

future<std::string> bar() {
    std::cout << "bar" << std::endl;
    co_return "Hello, World! from bar";
}

future_void_inline foo() {
    auto s = co_await bar();
    std::cout << "foo got: " << s << std::endl;
    co_return {};
}

asco_main future<int> async_main() {
    std::cout << "Hello, World!" << std::endl;
    co_await foo();
    co_return 0;
}
