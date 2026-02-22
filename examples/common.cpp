// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/runtime.h>
#include <asco/future.h>
#include <asco/util/type_id.h>

using namespace asco;

future<int> async_main() {
    std::size_t x = 42;
    co_await spawn([&x]() -> future<void> {
        std::println("{}", x);
        co_return;
    });
    co_return 0;
}
