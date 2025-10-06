// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

#include <asco/future.h>
#include <asco/nolock/continuous_queue.h>
#include <asco/print.h>
#include <asco/yield.h>

using asco::future;
namespace cq = asco::continuous_queue;

static void test_spsc_basic() {
    auto [tx, rx] = cq::create<int>();

    auto p0 = rx.pop();
    assert(!p0.has_value() && p0.error() == cq::pop_fail::non_object);

    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        auto r = tx.push(int{i});
        assert(!r.has_value());
    }

    for (int i = 0; i < N; ++i) {
        auto v = rx.pop();
        assert(v.has_value());
        assert(*v == i);
    }

    auto p1 = rx.pop();
    assert(!p1.has_value() && p1.error() == cq::pop_fail::non_object);
}

static void test_stop_semantics() {
    {
        auto [tx, rx] = cq::create<int>();
        rx.stop();
        int x = 42;
        auto r = tx.push(int{x});
        assert(r.has_value());
    }

    {
        auto [tx, rx] = cq::create<int>();
        for (int i = 0; i < 5; ++i) {
            auto r = tx.push(int{i});
            assert(!r.has_value());
        }
        tx.stop();

        for (int i = 0; i < 5; ++i) {
            auto v = rx.pop();
            while (!v.has_value() && v.error() == cq::pop_fail::non_object) {
                std::this_thread::yield();
                v = rx.pop();
            }
            assert(v.has_value() && *v == i);
        }

        auto p = rx.pop();
        int spin = 0;
        while (!p.has_value() && p.error() == cq::pop_fail::non_object && spin++ < 100000) {
            std::this_thread::yield();
            p = rx.pop();
        }
        assert(!p.has_value());
        assert(p.error() == cq::pop_fail::closed || p.error() == cq::pop_fail::non_object);
    }
}

static asco::future<void> mpmc_producer_task(cq::sender<std::int64_t> tx, int p, int per_producer) {
    const std::int64_t base = static_cast<std::int64_t>(p + 1) * 1000000LL;
    for (int i = 0; i < per_producer; ++i) {
        std::int64_t v = base + i;
        auto r = tx.push(std::move(v));
        if (r.has_value())
            co_return;
        if ((i & 0x3FF) == 0)
            co_await asco::yield<>();
    }
    co_return;
}

static asco::future<void> mpmc_consumer_task(
    cq::receiver<std::int64_t> rx, std::vector<std::int64_t> &bucket, std::atomic<std::int64_t> &consumed,
    std::int64_t total) {
    bucket.reserve(static_cast<size_t>(total / 4 + 8));
    while (true) {
        auto v = rx.pop();
        if (v.has_value()) {
            bucket.push_back(*v);
            consumed.fetch_add(1, std::memory_order_relaxed);
        } else if (v.error() == cq::pop_fail::non_object) {
            co_await asco::yield<>();
        } else {  // closed
            break;
        }
    }
    co_return;
}

static asco::future<void> test_mpmc_correctness_coro() {
    auto [tx0, rx0] = cq::create<std::int64_t>();

    constexpr int Producers = 4;
    constexpr int Consumers = 4;
    constexpr int PerProducer = 30000;
    const std::int64_t total = static_cast<std::int64_t>(Producers) * PerProducer;

    std::atomic<std::int64_t> consumed{0};
    std::vector<asco::future<void>> prod_futs;
    std::vector<asco::future<void>> cons_futs;

    std::vector<std::vector<std::int64_t>> buckets(Consumers);

    for (int c = 0; c < Consumers; ++c) {
        auto rx = rx0;  // each receiver copies handle
        cons_futs.emplace_back(mpmc_consumer_task(std::move(rx), buckets[c], consumed, total));
    }

    for (int p = 0; p < Producers; ++p) {
        auto tx = tx0;
        prod_futs.emplace_back(mpmc_producer_task(std::move(tx), p, PerProducer));
    }

    for (auto &f : prod_futs) co_await f;

    tx0.stop();

    for (auto &f : cons_futs) co_await f;

    rx0.stop();

    std::vector<std::int64_t> all;
    all.reserve(static_cast<size_t>(total));
    for (auto &b : buckets) { all.insert(all.end(), b.begin(), b.end()); }
    assert(static_cast<std::int64_t>(all.size()) == total);
    std::sort(all.begin(), all.end());
    auto it = std::unique(all.begin(), all.end());
    assert(std::distance(all.begin(), it) == total);

    co_return;
}

future<int> async_main() {
    asco::println("[continuous_queue] SPSC...");
    test_spsc_basic();
    asco::println("OK\n[continuous_queue] stop...");
    test_stop_semantics();
    asco::println("OK\n[continuous_queue] MPMC (coroutines)...");
    co_await test_mpmc_correctness_coro();
    asco::println("OK");
    co_return 0;
}
