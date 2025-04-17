// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/future.h>

#include <asco/sync/semaphore.h>

using asco::binary_semaphore;

future_void foo() {
    binary_semaphore coro_local(sem);
    std::cout << "foo release semaphore" << std::endl;
    sem.release();
    co_return {};
}

asco_main future<int> async_main() {
    binary_semaphore decl_local(sem, new binary_semaphore{0});
    foo();
    co_await sem.acquire();
    std::cout << "main acquire semaphore" << std::endl;
    sem.release();
    auto task = sem.acquire();
    task.abort();
    co_await task;
    std::cout << "test the abortable task (must be 1): " << sem.get_counter() << std::endl;
    co_return 0;
}
