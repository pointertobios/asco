// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <iostream>

#include <asco/future.h>
#include <asco/sync/barrier.h>

using asco::future;

constexpr size_t NUM_THREADS = 5;

future<void> worker(asco::sync::barrier<NUM_THREADS> &bar, size_t id) {
    std::cout << "Worker " << id << " arrived at barrier.\n";
    co_await bar.arrive().wait();
    std::cout << "Worker " << id << " passed the barrier.\n";
    co_return;
}

future<int> async_main() {
    asco::sync::barrier<NUM_THREADS> bar;

    std::cout << "Starting workers...\n";
    for (size_t i = 0; i < NUM_THREADS; ++i) { worker(bar, i + 1); }

    co_await bar.all_arrived();
    std::cout << "All workers have passed the barrier.\n";

    co_return 0;
}
