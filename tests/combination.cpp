// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <stdexcept>

#include <asco/future.h>

using asco::future;

asco::future<int> async_add(int a, int b) { co_return a + b; }

asco::future<int> async_throw() {
    throw std::runtime_error("");
    co_return 0;
}

future<int> async_main() {
    auto fut = async_add(1, 2).then([](int v) -> asco::future<int> { co_return v * 2; });
    int result = co_await fut;
    assert(result == 6);
    std::cout << "then passed\n";

    auto fut2 = async_throw().exceptionally([](const std::runtime_error &e) -> void {
        std::cout << "exceptionally caught: " << e.what() << std::endl;
    });
    co_await fut2;
    std::cout << "exceptionally passed\n";

    co_return 0;
}
