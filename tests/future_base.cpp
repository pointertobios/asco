// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <print>

#include <asco/concurrency/concurrency.h>
#include <asco/future.h>

using namespace asco;

future_spawn<void> wtf() {
    for (int i = 0; i < 10; ++i) {
        asco::concurrency::withdraw<100000>();
        std::println("In wtf()");
    }
    co_return;
}

future<int> foo() {
    co_await wtf();
    co_return 42;
}

future_spawn<void> bar() {
    std::println("In bar()");
    co_return;
}

future<int> async_main() {
    std::println("Hello, World! {}", co_await foo());
    co_await bar();
    std::println("Back to async_main()");
    co_return 0;
}
