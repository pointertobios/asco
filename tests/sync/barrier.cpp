// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/print.h>
#include <asco/sync/barrier.h>

using asco::future;

constexpr size_t NUM_THREADS = 5;

future<void> worker(asco::sync::barrier<NUM_THREADS> &bar, size_t id) {
    co_await asco::println("Worker {} arrived at barrier.", id);
    co_await bar.arrive().wait();
    co_await asco::println("Worker {} passed the barrier.", id);
    co_return;
}

future<int> async_main() {
    asco::sync::barrier<NUM_THREADS> bar;

    co_await asco::println("Starting workers...");
    for (size_t i = 0; i < NUM_THREADS; ++i) { worker(bar, i + 1); }

    co_await bar.all_arrived();
    co_await asco::println("All workers have passed the barrier.");

    co_return 0;
}
