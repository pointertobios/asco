// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <print>
#include <vector>

#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/sync/notify.h>
#include <asco/time/sleep.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    std::println("test_notify: start");

    // Test A: wait before notify_one
    {
        notify n;
        std::atomic<bool> waiting{false};
        std::atomic<bool> resumed{false};

        auto waiter = co_invoke([&n, &waiting, &resumed]() -> future_spawn<void> {
            waiting.store(true, std::memory_order_release);
            co_await n.wait();
            resumed.store(true, std::memory_order_release);
            co_return;
        });

        while (!waiting.load(std::memory_order_acquire)) { co_await sleep_for(1ms); }

        n.notify_one();
        co_await waiter;

        if (!resumed.load(std::memory_order_acquire)) {
            std::println("test_notify: A FAILED - waiter did not resume after notify_one");
            co_return 1;
        }
        std::println("test_notify: A passed");
    }

    // Test B: notify_one before wait should not unblock later waiters
    {
        notify n;
        std::atomic<bool> waiting{false};
        std::atomic<bool> resumed{false};

        n.notify_one();  // should have no effect because no waiter is registered yet

        auto waiter = co_invoke([&n, &waiting, &resumed]() -> future_spawn<void> {
            waiting.store(true, std::memory_order_release);
            co_await n.wait();
            resumed.store(true, std::memory_order_release);
            co_return;
        });

        while (!waiting.load(std::memory_order_acquire)) { co_await sleep_for(1ms); }

        co_await sleep_for(5ms);
        if (resumed.load(std::memory_order_acquire)) {
            std::println("test_notify: B FAILED - waiter resumed without notify");
            co_return 1;
        }

        n.notify_one();
        co_await waiter;

        if (!resumed.load(std::memory_order_acquire)) {
            std::println("test_notify: B FAILED - waiter did not resume after second notify");
            co_return 1;
        }
        std::println("test_notify: B passed");
    }

    // Test C: notify_all wakes every waiter exactly once
    {
        notify n;
        constexpr int N = 3;
        std::atomic<int> waiting{0};
        std::atomic<int> resumed{0};
        std::vector<future_spawn<void>> waiters;
        waiters.reserve(N);

        for (int i = 0; i < N; ++i) {
            waiters.push_back(co_invoke([&n, &waiting, &resumed]() -> future_spawn<void> {
                waiting.fetch_add(1, std::memory_order_acq_rel);
                co_await n.wait();
                resumed.fetch_add(1, std::memory_order_acq_rel);
                co_return;
            }));
        }

        while (waiting.load(std::memory_order_acquire) != N) { co_await sleep_for(1ms); }

        n.notify_all();
        for (auto &w : waiters) { co_await w; }

        if (resumed.load(std::memory_order_acquire) != N) {
            std::println(
                "test_notify: C FAILED - expected {} waiters resumed, got {}", N,
                resumed.load(std::memory_order_acquire));
            co_return 1;
        }
        std::println("test_notify: C passed");
    }

    std::println("test_notify: all checks passed");
    co_return 0;
}
