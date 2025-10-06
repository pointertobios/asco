// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/print.h>
#include <asco/select.h>
#include <asco/time/interval.h>

using asco::future;
using namespace std::chrono_literals;

future<int> async_main() {
    asco::println("async_main");
    asco::interval in1s{1s};
    asco::interval in500ms{500ms};
    for (int i{0}; i < 9; i++) {
        switch (co_await asco::select<2>{}) {
        case 0: {
            co_await in1s.tick();
            asco::println("1s");
        } break;
        case 1: {
            co_await in500ms.tick();
            asco::println("500ms");
        } break;
        }
    }
    asco::println("async_main exit");
    co_return 0;
}
