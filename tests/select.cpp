// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/select.h>
#include <asco/time/interval.h>

using asco::future;
using namespace std::chrono_literals;

future<int> async_main() {
    std::cout << "async_main\n";
    asco::interval in1s{1s};
    asco::interval in500ms{500ms};
    for (int i{0}; i < 9; i++) {
        switch (co_await asco::select<2>{}) {
        case 0: {
            co_await in1s.tick();
            std::cout << "1s\n";
            break;
        }
        case 1: {
            co_await in500ms.tick();
            std::cout << "500ms\n";
            break;
        }
        }
    }
    std::cout << "async_main exit\n";
    co_return 0;
}
