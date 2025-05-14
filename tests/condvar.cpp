// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <iostream>

#include <asco/future.h>
#include <asco/sync/condition_variable.h>

using asco::future, asco::future_void, asco::condition_variable;

future_void one_shoot() {
    std::cout << "one shoot test started\n";

    condition_variable decl_local(cv);

    bool decl_local(flag, new bool{false});

    auto t = [] -> future_void {
        bool coro_local(flag);
        condition_variable coro_local(cv);
        std::cout << "sub task waiting on cv\n";
        co_await cv.wait([flag] { return flag; });
        std::cout << "sub task notified\n";
        co_return {};
    }();

    flag = true;
    cv.notify_one();
    std::cout << "notify one on cv\n";

    co_await t;

    co_return {};
}

future_void broadcast_test() {
    std::cout << "broadcast test started\n";

    condition_variable decl_local(cv);

    std::atomic_bool decl_local(flag, new std::atomic_bool{false});

    auto task1 = [] -> future_void {
        std::atomic_bool coro_local(flag);
        condition_variable coro_local(cv);
        std::cout << "task1 waiting on cv\n";
        co_await cv.wait([&flag] { return flag.load(std::memory_order::seq_cst); });
        std::cout << "task1 notified\n";
        co_return {};
    }();

    auto task2 = [] -> future_void {
        std::atomic_bool coro_local(flag);
        condition_variable coro_local(cv);
        std::cout << "task2 waiting on cv\n";
        co_await cv.wait([&flag] { return flag.load(std::memory_order::seq_cst); });
        std::cout << "task2 notified\n";
        co_return {};
    }();

    auto task3 = [] -> future_void {
        std::atomic_bool coro_local(flag);
        condition_variable coro_local(cv);
        std::cout << "task3 waiting on cv\n";
        co_await cv.wait([&flag] { return flag.load(std::memory_order::seq_cst); });
        std::cout << "task3 notified\n";
        co_return {};
    }();

    flag.store(true, std::memory_order::seq_cst);
    cv.notify_all();
    cv.notify_all();
    cv.notify_all();
    std::cout << "broadcast notify all on cv\n";

    co_await task1;
    co_await task2;
    co_await task3;

    co_return {};
}

future<int> async_main() {
    co_await one_shoot();
    co_await broadcast_test();
    co_return 0;
}
