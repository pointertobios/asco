// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <asco/future.h>
#include <asco/select.h>

using asco::future;

future<int> async_main() {
    std::cout << "async_main\n";
    auto r = co_await asco::select<10>{};
    std::cout << std::format("{}\n", r);
    co_return 0;
}
