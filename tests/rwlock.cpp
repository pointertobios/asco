// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <vector>

#include <asco/future.h>
#include <asco/sync/rwlock.h>
#include <asco/time/sleep.h>

using asco::future, asco::future_void, asco::rwlock;
using namespace std::chrono_literals;

void test_constructors() {
    rwlock<int> value_lock(42);
    rwlock<std::string> string_lock("Hello, World!");
    rwlock<std::vector<int>> vector_lock({1, 2, 3});

    std::cout << "Constructors tested successfully." << std::endl;
}

future_void test_locks() {
    rwlock<int> lock(10);

    {
        auto read_guard = co_await lock.read();
        std::cout << "Read lock acquired: " << *read_guard << std::endl;
    }

    {
        auto write_guard = co_await lock.write();
        *write_guard = 20;
        std::cout << "Write lock acquired and value updated: " << *write_guard << std::endl;
    }

    co_return {};
}

future_void test_nested_locks() {
    rwlock<int> lock(10);

    {
        auto read_guard1 = co_await lock.read();
        auto read_guard2 = co_await lock.read();
        std::cout << "Nested read locks acquired: " << *read_guard1 << ", " << *read_guard2 << std::endl;
    }

    {
        auto write_guard1 = co_await lock.write();
        auto write_guard2 = lock.try_write();
        std::cout << "Nested write locks acquired: " << *write_guard1 << ", ";
        if (write_guard2) {
            std::cout << **write_guard2;
        } else {
            std::cout << "none";
        }
        std::cout << std::endl;
    }

    co_return {};
}

future_void test_mutex_locks() {
    rwlock<int> lock(10);

    asco::future_void task;
    {
        auto write_guard = co_await lock.write();
        std::cout << "Write lock acquired: " << *write_guard << std::endl;

        task = [&]() -> future_void {
            auto read_guard = co_await lock.read();
            std::cout << "Read lock acquired after write lock released: " << *read_guard << std::endl;
            co_return {};
        }();

        co_await asco::this_coro::sleep_for(1s);
    }
    std::cout << "Write lock released" << std::endl;
    co_await task;

    co_return {};
}

future_void test_exception_handling() {
    rwlock<int> lock(10);

    try {
        auto write_guard = co_await lock.write();
        throw std::runtime_error("Exception occurred while holding write lock");
    } catch (const std::exception &e) { std::cout << "Exception caught: " << e.what() << std::endl; }

    {
        auto read_guard = co_await lock.read();
        std::cout << "Read lock acquired after exception: " << *read_guard << std::endl;
    }

    co_return {};
}

future<int> async_main() {
    test_constructors();

    co_await test_locks();

    co_await test_nested_locks();

    co_await test_mutex_locks();

    co_await test_exception_handling();

    co_return 0;
}
