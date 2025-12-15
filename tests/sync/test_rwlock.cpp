// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <print>
#include <string>
#include <vector>

#include <asco/concurrency/concurrency.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/sync/rwlock.h>
#include <asco/time/sleep.h>
#include <asco/utils/defines.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    std::println("test_rwlock: start");

    // Test A: read guard move semantics reflect shared ownership transfer
    {
        rwlock<> lock;
        with(auto guard = co_await lock.read()) {
            auto moved = std::move(guard);
            if (guard) {
                std::println("test_rwlock: A FAILED - moved-from read guard still valid");
                co_return 1;
            }
            if (!moved) {
                std::println("test_rwlock: A FAILED - moved-to read guard invalid");
                co_return 1;
            }
        }
        else {
            std::println("test_rwlock: A FAILED - initial read guard invalid");
            co_return 1;
        }
        std::println("test_rwlock: A passed");
    }

    // Test B: multiple readers may coexist concurrently
    {
        rwlock<> lock;
        std::atomic<int> active_readers{0};
        std::atomic<int> max_readers{0};
        std::atomic<int> failures{0};
        constexpr int reader_count = 6;
        std::vector<future_spawn<void>> readers;
        readers.reserve(reader_count);

        for (int i = 0; i < reader_count; ++i) {
            readers.push_back(co_invoke([&]() -> future_spawn<void> {
                with(auto guard = co_await lock.read()) {
                    auto current = active_readers.fetch_add(1, std::memory_order_acq_rel) + 1;
                    auto prev_max = max_readers.load(std::memory_order_relaxed);
                    while (current > prev_max
                           && !max_readers.compare_exchange_weak(
                               prev_max, current, std::memory_order_acq_rel, std::memory_order_relaxed)) {}
                    if (!guard) {
                        failures.fetch_add(1, std::memory_order_acq_rel);
                    }
                    co_await sleep_for(2ms);
                    active_readers.fetch_sub(1, std::memory_order_acq_rel);
                }
                else {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                }
                co_return;
            }));
        }

        for (auto &r : readers) { co_await r; }

        if (failures.load(std::memory_order_acquire) != 0) {
            std::println("test_rwlock: B FAILED - guard acquisition failure");
            co_return 1;
        }
        if (max_readers.load(std::memory_order_acquire) < 2) {
            std::println("test_rwlock: B FAILED - readers did not overlap");
            co_return 1;
        }
        if (active_readers.load(std::memory_order_acquire) != 0) {
            std::println("test_rwlock: B FAILED - reader counter leaked");
            co_return 1;
        }
        std::println("test_rwlock: B passed");
    }

    // Test C: writers exclude readers and other writers
    {
        rwlock<> lock;
        std::atomic<int> active_readers{0};
        std::atomic<int> active_writers{0};
        std::atomic<int> violations{0};
        constexpr int reader_tasks = 4;
        constexpr int writer_tasks = 2;
        constexpr int iterations = 40;
        std::vector<future_spawn<void>> workers;
        workers.reserve(reader_tasks + writer_tasks);

        for (int i = 0; i < reader_tasks; ++i) {
            workers.push_back(co_invoke([&]() -> future_spawn<void> {
                for (int j = 0; j < iterations; ++j) {
                    with(auto guard = co_await lock.read()) {
                        active_readers.fetch_add(1, std::memory_order_acq_rel);
                        concurrency::withdraw<8>();
                        if (active_writers.load(std::memory_order_acquire) != 0) {
                            violations.fetch_add(1, std::memory_order_acq_rel);
                        }
                        active_readers.fetch_sub(1, std::memory_order_acq_rel);
                        concurrency::withdraw<4>();
                    }
                    else {
                        violations.fetch_add(1, std::memory_order_acq_rel);
                    }
                    co_await sleep_for(1ms);
                }
                co_return;
            }));
        }

        for (int i = 0; i < writer_tasks; ++i) {
            workers.push_back(co_invoke([&]() -> future_spawn<void> {
                for (int j = 0; j < iterations; ++j) {
                    with(auto guard = co_await lock.write()) {
                        auto previous = active_writers.fetch_add(1, std::memory_order_acq_rel);
                        if (previous != 0) {
                            violations.fetch_add(1, std::memory_order_acq_rel);
                        }
                        if (active_readers.load(std::memory_order_acquire) != 0) {
                            violations.fetch_add(1, std::memory_order_acq_rel);
                        }
                        co_await sleep_for(1ms);
                        active_writers.fetch_sub(1, std::memory_order_acq_rel);
                    }
                    else {
                        violations.fetch_add(1, std::memory_order_acq_rel);
                    }
                    co_await sleep_for(1ms);
                }
                co_return;
            }));
        }

        for (auto &worker : workers) { co_await worker; }

        if (violations.load(std::memory_order_acquire) != 0) {
            std::println("test_rwlock: C FAILED - writer exclusivity violated");
            co_return 1;
        }
        if (active_readers.load(std::memory_order_acquire) != 0
            || active_writers.load(std::memory_order_acquire) != 0) {
            std::println("test_rwlock: C FAILED - active counters leaked");
            co_return 1;
        }
        std::println("test_rwlock: C passed");
    }

    // Test D: rwlock<T> provides guarded access to wrapped value
    {
        rwlock<std::string> lock{std::string{"hello"}};
        with(auto guard = co_await lock.read()) {
            if (!guard || guard->compare("hello") != 0) {
                std::println("test_rwlock: D FAILED - read guard value incorrect");
                co_return 1;
            }
        }
        else {
            std::println("test_rwlock: D FAILED - read guard acquisition");
            co_return 1;
        }

        with(auto guard = co_await lock.write()) {
            if (!guard) {
                std::println("test_rwlock: D FAILED - write guard invalid");
                co_return 1;
            }
            *guard = "world";
            auto moved = std::move(guard);
            if (guard || !moved || moved->compare("world") != 0) {
                std::println("test_rwlock: D FAILED - write guard move semantics");
                co_return 1;
            }
        }
        else {
            std::println("test_rwlock: D FAILED - write guard acquisition");
            co_return 1;
        }

        with(auto guard = co_await lock.read()) {
            if (!guard || *guard != "world") {
                std::println("test_rwlock: D FAILED - updated value not visible");
                co_return 1;
            }
        }
        else {
            std::println("test_rwlock: D FAILED - second read guard acquisition");
            co_return 1;
        }

        std::println("test_rwlock: D passed");
    }

    // Test E: high-concurrency readers and writers preserve monotonic progress
    {
        rwlock<int> lock{0};
        constexpr int reader_tasks = 16;
        constexpr int writer_tasks = 8;
        constexpr int reader_iterations = 200;
        constexpr int writer_iterations = 120;
        std::atomic<int> reader_failures{0};
        std::atomic<int> writer_failures{0};
        std::vector<future_spawn<void>> workers;
        workers.reserve(reader_tasks + writer_tasks);

        for (int i = 0; i < reader_tasks; ++i) {
            workers.push_back(co_invoke([&, i]() -> future_spawn<void> {
                int last_seen = -1;
                for (int j = 0; j < reader_iterations; ++j) {
                    with(auto guard = co_await lock.read()) {
                        if (!guard) {
                            reader_failures.fetch_add(1, std::memory_order_acq_rel);
                            break;
                        }
                        int value = *guard;
                        if (value < last_seen) {
                            reader_failures.fetch_add(1, std::memory_order_acq_rel);
                            break;
                        }
                        last_seen = value;
                    }
                    else {
                        reader_failures.fetch_add(1, std::memory_order_acq_rel);
                        break;
                    }
                    if ((j + i) % 16 == 0) {
                        co_await sleep_for(1ms);
                    } else {
                        concurrency::withdraw<4>();
                    }
                }
                co_return;
            }));
        }

        for (int i = 0; i < writer_tasks; ++i) {
            workers.push_back(co_invoke([&, i]() -> future_spawn<void> {
                for (int j = 0; j < writer_iterations; ++j) {
                    with(auto guard = co_await lock.write()) {
                        if (!guard) {
                            writer_failures.fetch_add(1, std::memory_order_acq_rel);
                            break;
                        }
                        ++(*guard);
                    }
                    else {
                        writer_failures.fetch_add(1, std::memory_order_acq_rel);
                        break;
                    }
                    if ((j + i) % 8 == 0) {
                        co_await sleep_for(1ms);
                    } else {
                        concurrency::withdraw<8>();
                    }
                }
                co_return;
            }));
        }

        for (auto &worker : workers) { co_await worker; }

        int final_value = -1;
        with(auto guard = co_await lock.read()) { final_value = *guard; }
        else {
            final_value = -1;
        }

        if (reader_failures.load(std::memory_order_acquire) != 0) {
            std::println("test_rwlock: E FAILED - reader invariant broken");
            co_return 1;
        }
        if (writer_failures.load(std::memory_order_acquire) != 0) {
            std::println("test_rwlock: E FAILED - writer guard acquisition failure");
            co_return 1;
        }
        if (final_value != writer_tasks * writer_iterations) {
            std::println(
                "test_rwlock: E FAILED - inconsistent final value {} vs expected {}", final_value,
                writer_tasks * writer_iterations);
            co_return 1;
        }
        std::println("test_rwlock: E passed");
    }

    std::println("test_rwlock: all checks passed");
    co_return 0;
}
