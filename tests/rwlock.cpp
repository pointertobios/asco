// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <asco/future.h>
#include <asco/sync/rwlock.h>

using asco::future, asco::rwlock;

future<int> async_main() {
    rwlock<int> lk{10};
    {
        auto g1 = co_await lk.read();
        auto g2 = co_await lk.read();
        std::cout << *g1 << std::endl;
        std::cout << *g2 << std::endl;
        // auto g3 = co_await lk.write();  // cannot get it
    }
    {
        auto g = co_await lk.write();
        *g = 20;
        std::cout << *g << std::endl;
        // auto g1 = co_await lk.write();  // cannot get it
        // auto g1 = co_await lk.read();  // cannot get it
    }
    co_return 0;
}
