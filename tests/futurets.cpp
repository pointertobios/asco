// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>

#include <asco/future.h>
#include <asco/print.h>

using asco::future, asco::future_inline, asco::future_core;

future_inline<uint64_t> foo(uint64_t i) {
    char coro_local(y);
    char *coro_local(str);
    co_await asco::print("foo {}", y);
    y = i % 26 + 'a';
    co_return i;
}

future_core<void> bar() {
    char decl_local(y, new char{'a'});
    char *decl_local_array(str, new char[10]);
    uint64_t s = 0;
    for (uint64_t i = 1; i <= 10000; i++) {
        auto x = co_await foo(i);
        assert(x == i);
        s += x;
        co_await asco::println(" {}", y);
        co_await asco::println("{} : {}", x, s);
        y = 'a';
    }
    co_return;
}

future<int> async_main() {
    co_await asco::println("async_main");
    co_await bar();
    co_return 0;
}
