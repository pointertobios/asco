// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <stdexcept>

#include <asco/future.h>
#include <asco/print.h>

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
    asco::println("then passed");

    auto fut2 = co_await async_throw().exceptionally([](const std::runtime_error &e) -> void {
        // Fire-and-forget logging (no await inside non-coroutine handler)
        asco::println("exceptionally caught: {}", e.what());
    });
    assert(!fut2);  // must be the error type of expected<T, E>
    asco::println("exceptionally passed");

    co_return 0;
}
