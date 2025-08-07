// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <vector>

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/rterror.h>
#include <asco/sync/rwlock.h>
#include <asco/time/sleep.h>

using asco::future, asco::future, asco::rwlock;
using namespace std::chrono_literals;

void test_constructors() {
    rwlock<int> value_lock(42);
    rwlock<std::string> string_lock("Hello, World!");
    rwlock<std::vector<int>> vector_lock({1, 2, 3});

    std::cout << "Constructors tested successfully." << std::endl;
}

future<void> test_locks() {
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

    co_return;
}

future<void> test_nested_locks() {
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

    co_return;
}

future<void> test_mutex_locks() {
    rwlock<int> decl_local(lock, new rwlock<int>(10));

    auto task = co_await lock.write().then([](rwlock<int>::write_guard write_guard) -> future<future<void>> {
        std::cout << "Write lock acquired: " << *write_guard << std::endl;

        auto task = []() -> future<void> {
            rwlock<int> coro_local(lock);
            auto read_guard = co_await lock.read();
            std::cout << "Read lock acquired after write lock released: " << *read_guard << std::endl;
            assert(*read_guard == 10);
            co_return;
        }();
        co_await asco::this_coro::sleep_for(1s);
        std::cout << "Write lock released" << std::endl;
        co_return task;
    });

    co_await task;

    co_return;
}

future<void> test_exception_handling() {
    rwlock<int> lock(10);

    try {
        auto write_guard = co_await lock.write();
    } catch (const std::exception &e) { std::cout << "Exception caught: " << e.what() << std::endl; }

    {
        auto read_guard = co_await lock.read();
        std::cout << "Read lock acquired after exception: " << *read_guard << std::endl;
    }

    co_return;
}

future<int> async_main() {
    test_constructors();

    co_await test_locks();

    co_await test_nested_locks();

    co_await test_mutex_locks();

    co_await test_exception_handling();

    co_return 0;
}
