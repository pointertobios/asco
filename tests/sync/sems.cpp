// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>

#include <asco/future.h>
#include <asco/print.h>
#include <asco/sync/mutex.h>
#include <asco/sync/semaphore.h>
#include <asco/time/interval.h>

using asco::future, asco::binary_semaphore, asco::mutex;
using namespace std::chrono_literals;

future<void> foo() {
    binary_semaphore coro_local(sem);
    sem.release();
    asco::println("foo release semaphore, counter: {}", sem.get_counter());
    co_return;
}

future<void> bar() {
    binary_semaphore coro_local(sem);
    for (int i = 0; i < 100; i++) {
        sem.release();
        asco::println("bar after release {} counter: {}", i, sem.get_counter());
        co_await sem.acquire();
    }
    sem.release();
    asco::println("bar release, counter: {}", sem.get_counter());
    co_return;
}

future<void> fot() {
    mutex<int> coro_local(mut);
    asco::println("fot lock");
    auto g = co_await mut.lock();
    *g = 5;
    asco::println("fot *g: {}", *g);
    co_return;
}

future<int> async_main() {
    binary_semaphore decl_local(sem, new binary_semaphore{0});
    mutex<int> decl_local(mut);
    auto tt = foo();
    co_await sem.acquire();
    co_await tt;
    asco::println("main acquire semaphore, try acquire: {}", (sem.try_acquire() ? "true" : "false"));
    sem.release();
    if (sem.try_acquire()) {
        asco::println("main try acquire semaphore success: {}", sem.get_counter());
        sem.release();
    }
    auto task = sem.acquire();
    task.abort();
    co_await std::move(task).aborted([] {
        // Fire-and-forget logging, cannot co_await here
        asco::println("sem.acquire() truely aborted");
    });
    asco::println("test the abortable task (must be 1): {}", sem.get_counter());
    assert(sem.get_counter() == 1);
    co_await sem.acquire();

    future<void> t = bar();
    for (int i = 0; i < 100; i++) {
        co_await sem.acquire();
        asco::println("{} counter: {}", i, sem.get_counter());
        sem.release();
        asco::println("release, counter: {}", sem.get_counter());
    }
    co_await t;

    {
        auto g = co_await mut.lock();
        t = fot();
        asco::println("*g: {}", *g);
    }
    co_await t;

    asco::println("async_main will exit");
    co_return 0;
}
