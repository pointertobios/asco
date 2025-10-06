// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/print.h>
#include <asco/sync/condition_variable.h>
#include <asco/time/sleep.h>
#include <atomic>

using asco::future, asco::condition_variable;
using namespace std::chrono_literals;

future<void> one_shoot() {
    asco::println("one shoot test started");

    condition_variable decl_local(cv);

    bool decl_local(flag, new bool{false});

    auto t = [] -> future<void> {
        bool coro_local(flag);
        condition_variable coro_local(cv);
        asco::println("sub task waiting on cv");
        co_await cv.wait([flag] { return flag; });
        asco::println("sub task notified");
        co_return;
    }();

    flag = true;
    cv.notify_one();
    asco::println("notify one on cv");

    co_await t;

    co_return;
}

future<void> broadcast_test() {
    asco::println("broadcast test started");

    condition_variable decl_local(cv);

    std::atomic_bool decl_local(flag, new std::atomic_bool{false});

    auto task1 = [] -> future<void> {
        std::atomic_bool coro_local(flag);
        condition_variable coro_local(cv);
        asco::println("task1 waiting on cv");
        co_await cv.wait([&flag] { return flag.load(std::memory_order::seq_cst); });
        asco::println("task1 notified");
        co_return;
    }();

    auto task2 = [] -> future<void> {
        std::atomic_bool coro_local(flag);
        condition_variable coro_local(cv);
        asco::println("task2 waiting on cv");
        co_await cv.wait([&flag] { return flag.load(std::memory_order::seq_cst); });
        asco::println("task2 notified");
        co_return;
    }();

    auto task3 = [] -> future<void> {
        std::atomic_bool coro_local(flag);
        condition_variable coro_local(cv);
        asco::println("task3 waiting on cv");
        co_await cv.wait([&flag] { return flag.load(std::memory_order::seq_cst); });
        asco::println("task3 notified");
        co_return;
    }();

    flag.store(true, std::memory_order::seq_cst);
    co_await asco::this_coro::sleep_for(1s);
    cv.notify_all();
    asco::println("broadcast notify all on cv");

    co_await task1;
    co_await task2;
    co_await task3;

    co_return;
}

future<int> async_main() {
    co_await one_shoot();
    co_await broadcast_test();
    co_return 0;
}
