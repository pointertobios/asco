// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <asco/future.h>

future<uint64_t> foo(uint64_t i) {
    std::cout << "foo" << std::endl;
    co_return i;
}

asco_main future<int> async_main() {
    std::cout << "async_main" << std::endl;
    uint64_t s = 0;
    for (uint64_t i = 1; i <= 100000; i++) {
        s += co_await foo(i);
        std::cout << s << std::endl;
    }
    co_return 0;
}
