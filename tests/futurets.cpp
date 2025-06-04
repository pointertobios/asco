// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>

#include <asco/future.h>

using asco::future, asco::future_inline, asco::future_void_core;

future_inline<uint64_t> foo(uint64_t i) {
    char coro_local(y);
    char *coro_local(str);
    std::cout << "foo " << y;
    y = i % 26 + 'a';
    co_return i;
}

future_void_core bar() {
    char decl_local(y, new char{'a'});
    char *decl_local_array(str, new char[10]);
    uint64_t s = 0;
    for (uint64_t i = 1; i <= 100000; i++) {
        auto x = co_await foo(i);
        assert(x == i);
        s += x;
        std::cout << ' ' << y << std::endl;
        std::cout << x << " : " << s << std::endl;
        y = 'a';
    }
    co_return {};
}

future<int> async_main() {
    std::cout << "async_main" << std::endl;
    co_await bar();
    co_return 0;
}
