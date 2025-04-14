// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <asco/future.h>

future<uint64_t> foo(uint64_t i) {
    char coro_local(y);
    std::cout << "foo " << y;
    y = 'c';
    co_return i;
}

asco_main future<int> async_main() {
    char decl_local(y, new char{'a'});
    std::cout << "async_main" << std::endl;
    uint64_t s = 0;
    for (uint64_t i = 1; i <= 100000; i++) {
        auto x = co_await foo(i);
        if (x != i) {
            std::cout << std::endl << x << std::endl;
            break;
        }
        s += x;
        std::cout << ' ' << y << std::endl;
        std::cout << x << " : " << s << std::endl;
        y = 'a';
    }
    co_return 0;
}
