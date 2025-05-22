// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/exception.h>
#include <asco/future.h>

#include <iostream>

using asco::future;

void bar() { throw asco::exception("test error"); }

template<typename T>
asco::future_core<std::vector<int>> foo(T) {
    bar();
    co_return {};
}

future<int> async_main() {
    co_await foo<std::vector<int>>({});
    co_return 0;
}
