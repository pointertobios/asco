// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <print>

#include <asco/concurrency/concurrency.h>
#include <asco/future.h>
#include <asco/sync/semaphore.h>

using namespace asco;

future<int> async_main() {
    std::println("test_semaphore: start");

    // Test A: notify (release) before wait
    {
        binary_semaphore sem{0};
        sem.release();           // notify before wait
        co_await sem.acquire();  // should not suspend forever
        std::println("test_semaphore: notify-before-wait passed");
    }

    // Test B: wait before notify
    {
        binary_semaphore sem{0};
        std::atomic<bool> done{false};

        auto waiter_f = [&sem, &done]() -> future_spawn<void> {
            co_await sem.acquire();
            done.store(true, std::memory_order::release);
            std::println("test_semaphore: waiter resumed");
            co_return;
        };
        auto waiter = waiter_f();

        // Give runtime a short spin to ensure task is scheduled if needed.
        // concurrency::withdraw<100>();

        sem.release();    // wake the waiter
        co_await waiter;  // wait for waiter to finish

        if (!done.load(std::memory_order::acquire)) {
            std::println("test_semaphore: wait-before-notify FAILED");
            co_return 1;
        }
        std::println("test_semaphore: wait-before-notify passed");
    }

    // Test C: Large quantity of concurrency acquire and release
    {
        constexpr size_t N = 1000;
        counting_semaphore<N> sem{0};
        std::atomic<size_t> counter{0};

        auto worker_f = [&sem, &counter]() -> future_spawn<void> {
            for (size_t i{0}; i < N; ++i) {
                co_await sem.acquire();
                counter.fetch_add(1, std::memory_order::release);
            }
            co_return;
        };
        auto worker = worker_f();

        // Give runtime a short spin to ensure task is scheduled if needed.
        // concurrency::withdraw<100>();

        std::println("test_semaphore: releasing {} times", N);

        sem.release(N);

        co_await worker;

        if (auto c = counter.load(std::memory_order::acquire); c != N) {
            std::println("test_semaphore: large concurrency acquire/release FAILED: {}", c);
            co_return 1;
        }
        std::println("test_semaphore: large concurrency acquire/release passed");
    }

    std::println("test_semaphore: all checks passed");
    co_return 0;
}
