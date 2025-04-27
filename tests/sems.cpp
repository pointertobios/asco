// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <iostream>

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/sync/mutex.h>
#include <asco/sync/semaphore.h>
#include <asco/time/interval.h>

using asco::future, asco::future_void, asco::binary_semaphore, asco::mutex, asco::interval;
using namespace std::chrono_literals;

future_void foo() {
    binary_semaphore coro_local(sem);
    sem.release();
    std::cout << "foo release semaphore, counter: " << sem.get_counter() << std::endl;
    co_return {};
}

future_void bar() {
    binary_semaphore coro_local(sem);
    for (int i = 0; i < 100; i++) {
        sem.release();
        std::cout << "bar after release " << i << " counter: " << sem.get_counter() << std::endl;
        co_await sem.acquire();
    }
    sem.release();
    std::cout << "bar release, counter: " << sem.get_counter() << std::endl;
    co_return {};
}

future_void fot() {
    mutex<int> coro_local(mut);
    std::cout << "fot lock" << std::endl;
    auto g = co_await mut.lock();
    *g = 5;
    std::cout << "fot *g: " << *g << std::endl;
    co_return {};
}

future<int> async_main() {
    binary_semaphore decl_local(sem, new binary_semaphore{0});
    mutex<int> decl_local(mut);
    auto tt = foo();
    co_await sem.acquire();
    co_await tt;
    std::cout << "main acquire semaphore, try acquire: " << (sem.try_acquire() ? "true" : "false")
              << std::endl;
    sem.release();
    if (sem.try_acquire()) {
        std::cout << "main try acquire semaphore success: " << sem.get_counter() << std::endl;
        sem.release();
    }
    auto task = sem.acquire();
    task.abort();
    co_await task;
    std::cout << "test the abortable task (must be 1): " << sem.get_counter() << std::endl;
    assert(sem.get_counter() == 1);
    co_await sem.acquire();

    future_void t = bar();
    for (int i = 0; i < 100; i++) {
        co_await sem.acquire();
        std::cout << i << " counter: " << sem.get_counter() << std::endl;
        sem.release();
        std::cout << "release, counter: " << sem.get_counter() << std::endl;
    }
    co_await t;

    {
        auto g = co_await mut.lock();
        t = fot();
        std::cout << "*g: " << *g << std::endl;
    }
    co_await t;

    std::cout << "async_main will exit" << std::endl;
    co_return 0;
}
