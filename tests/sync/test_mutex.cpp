// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <print>
#include <string>
#include <vector>

#include <asco/concurrency/concurrency.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/sync/mutex.h>
#include <asco/utils/defines.h>

using namespace asco;

future<int> async_main() {
    std::println("test_mutex: start");

    // Test A: guard move semantics reflect ownership transfer
    {
        sync::mutex<> mtx;
        with(auto guard = co_await mtx.lock()) {
            auto moved = std::move(guard);
            if (guard) {
                std::println("test_mutex: move semantics FAILED - moved-from guard still true");
                co_return 1;
            }
            if (!moved) {
                std::println("test_mutex: move semantics FAILED - moved-to guard false");
                co_return 1;
            }
        }
        else {
            std::println("test_mutex: move semantics FAILED - initial guard invalid");
            co_return 1;
        }
        std::println("test_mutex: move semantics passed");
    }

    // Test B: lock releases on destruction and can be reacquired
    {
        sync::mutex<> mtx;
        with(auto guard = co_await mtx.lock()) {
            // Guard left intentionally unused; scope exit releases the lock.
        }
        else {
            std::println("test_mutex: reacquire FAILED - initial lock invalid");
            co_return 1;
        }
        with(auto guard = co_await mtx.lock()) {
            // Successful reacquire ensures mutex unlocks on guard destruction.
        }
        else {
            std::println("test_mutex: reacquire FAILED - second lock invalid");
            co_return 1;
        }
        std::println("test_mutex: reacquire passed");
    }

    // Test C: mutual exclusion under contention
    {
        sync::mutex<> mtx;
        std::atomic<int> active{0};
        std::atomic<int> violations{0};
        int counter = 0;
        constexpr int worker_count = 8;
        constexpr int iterations = 200;
        std::vector<future_spawn<void>> workers;
        workers.reserve(worker_count);

        for (int i = 0; i < worker_count; ++i) {
            workers.push_back(co_invoke([&]() -> future_spawn<void> {
                for (int j = 0; j < iterations; ++j) {
                    with(auto guard = co_await mtx.lock()) {
                        auto prev = active.fetch_add(1, std::memory_order_acq_rel);
                        if (prev != 0) {
                            violations.fetch_add(1, std::memory_order_acq_rel);
                        }
                        ++counter;
                        concurrency::withdraw<8>();
                        active.fetch_sub(1, std::memory_order_acq_rel);
                    }
                    else {
                        violations.fetch_add(1, std::memory_order_acq_rel);
                    }
                }
                co_return;
            }));
        }

        for (auto &worker : workers) { co_await worker; }

        auto v = violations.load(std::memory_order_acquire);
        if (v != 0) {
            std::println("test_mutex: mutual exclusion FAILED - violations {}", v);
            co_return 1;
        }
        if (counter != worker_count * iterations) {
            std::println("test_mutex: mutual exclusion FAILED - counter {}", counter);
            co_return 1;
        }
        std::println("test_mutex: mutual exclusion passed");
    }

    // Test D: mutex<T> guard exposes value and preserves move semantics
    {
        sync::mutex<std::string> mtx{std::string("hello")};
        with(auto guard = co_await mtx.lock()) {
            if (*guard != "hello") {
                std::println("test_mutex: value guard FAILED - initial state");
                co_return 1;
            }
            *guard = "world";
            auto moved = std::move(guard);
            if (guard || !moved || *moved != "world") {
                std::println("test_mutex: value guard FAILED - move semantics");
                co_return 1;
            }
        }
        else {
            std::println("test_mutex: value guard FAILED - initial lock invalid");
            co_return 1;
        }
        with(auto guard = co_await mtx.lock()) {
            if (*guard != "world") {
                std::println("test_mutex: value guard FAILED - persistence");
                co_return 1;
            }
        }
        else {
            std::println("test_mutex: value guard FAILED - persistence lock invalid");
            co_return 1;
        }
        std::println("test_mutex: value guard passed");
    }

    std::println("test_mutex: all checks passed");
    co_return 0;
}
