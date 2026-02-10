// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include <thread>
#include <vector>

#include <asco/concurrency/ring_queue.h>

namespace {

using clock_type = std::chrono::steady_clock;

struct test_failure : std::exception {
    const char *what() const noexcept override { return "test failed"; }
};

[[noreturn]] void fail(const char *expr, const char *file, int line) {
    std::println("[ring_queue test] FAIL: {} @ {}:{}", expr, file, line);
    throw test_failure{};
}

#define CHECK(expr)                          \
    do {                                     \
        if (!(expr))                         \
            fail(#expr, __FILE__, __LINE__); \
    } while (false)

template<typename Sender, typename T>
void push_blocking(Sender &tx, T &&value, clock_type::time_point deadline) {
    for (;;) {
        auto r = tx.try_send(std::forward<T>(value));
        if (!r.has_value()) {
            return;
        }
        value = std::move(*r);
        if (clock_type::now() > deadline) {
            fail("push_blocking timeout", __FILE__, __LINE__);
        }
        std::this_thread::yield();
    }
}

template<typename Receiver>
auto pop_blocking(Receiver &rx, clock_type::time_point deadline) {
    for (;;) {
        auto r = rx.try_recv();
        if (r.has_value()) {
            return *r;
        }
        if (clock_type::now() > deadline) {
            fail("pop_blocking timeout", __FILE__, __LINE__);
        }
        std::this_thread::yield();
    }
}

void test_basic_single_thread() {
    auto [tx, rx] = asco::concurrency::ring_queue::create<int, 8>();

    CHECK(!rx.try_recv().has_value());
    CHECK(!tx.try_send(42).has_value());
    auto r = rx.try_recv();
    CHECK(r.has_value());
    CHECK(*r == 42);
    CHECK(!rx.try_recv().has_value());
}

void test_capacity_full_empty() {
    constexpr std::size_t cap = 2;
    auto [tx, rx] = asco::concurrency::ring_queue::create<int, cap>();

    CHECK(!tx.try_send(1).has_value());
    CHECK(!tx.try_send(2).has_value());

    // 队列已满。
    auto fail_val = tx.try_send(3);
    CHECK(fail_val.has_value());
    CHECK(*fail_val == 3);

    auto a = rx.try_recv();
    auto b = rx.try_recv();
    CHECK(a.has_value() && b.has_value());
    CHECK(!rx.try_recv().has_value());
}

void test_spsc_order_and_wrap() {
    // 使用较小的容量来强制触发环绕。
    constexpr std::size_t cap = 8;
    auto [tx, rx] = asco::concurrency::ring_queue::create<std::int64_t, cap>();

    auto deadline = clock_type::now() + std::chrono::seconds(2);
    for (std::int64_t i = 0; i < 10'000; i++) {
        push_blocking(tx, i, deadline);
        auto v = pop_blocking(rx, deadline);
        CHECK(v == i);
    }
}

struct move_only {
    std::unique_ptr<int> p;
    explicit move_only(int v)
            : p(std::make_unique<int>(v)) {}
    move_only(move_only &&) = default;
    move_only &operator=(move_only &&) noexcept = default;
    move_only(const move_only &) = delete;
    move_only &operator=(const move_only &) = delete;
};

void test_move_only_type() {
    auto [tx, rx] = asco::concurrency::ring_queue::create<move_only, 16>();

    CHECK(!tx.try_send(move_only{7}).has_value());
    auto r = rx.try_recv();
    CHECK(r.has_value());
    CHECK((*r).p);
    CHECK(*(*r).p == 7);
}

struct throwing_move {
    static inline std::atomic_int move_count{0};
    int value{0};

    explicit throwing_move(int v)
            : value(v) {}

    throwing_move(const throwing_move &) = default;
    throwing_move &operator=(const throwing_move &) = default;

    throwing_move(throwing_move &&other) {
        // 只在第一次真实 move 时抛异常，用于测试异常路径恢复。
        if (move_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            throw std::runtime_error{"move throw"};
        }
        value = other.value;
        other.value = 0;
    }

    throwing_move &operator=(throwing_move &&other) {
        if (move_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            throw std::runtime_error{"move throw"};
        }
        value = other.value;
        other.value = 0;
        return *this;
    }
};

void test_exception_path_recovers() {
    throwing_move::move_count.store(0, std::memory_order_relaxed);
    auto [tx, rx] = asco::concurrency::ring_queue::create<throwing_move, 8>();

    bool threw = false;
    try {
        // 传入左值，避免在按值传参时就发生 move。
        // 期望第一次 move 发生在队列内部存储赋值处。
        throwing_move x{123};
        (void)tx.try_send(x);
    } catch (const std::runtime_error &) { threw = true; }
    CHECK(threw);

    // 消费者应能观察到并清理异常槽位。
    CHECK(!rx.try_recv().has_value());

    // 清理之后，队列应该可以继续使用（同一个类型）。
    // （后续 move 不再抛异常，因为 move_count 已经 > 0。）
    throwing_move y{7};
    CHECK(!tx.try_send(y).has_value());
    auto r = rx.try_recv();
    CHECK(r.has_value());
    CHECK(r->value == 7);
}

void test_void_specialization() {
    constexpr std::size_t cap = 4;
    auto [tx, rx] = asco::concurrency::ring_queue::create<void, cap>();

    // 空队列 pop。
    CHECK(rx.try_recv() == false);

    // 填满队列。
    CHECK(tx.try_send() == false);
    CHECK(tx.try_send() == false);
    CHECK(tx.try_send() == false);
    CHECK(tx.try_send() == false);
    // 队列已满。
    CHECK(tx.try_send() == true);

    // 清空队列。
    CHECK(rx.try_recv() == true);
    CHECK(rx.try_recv() == true);
    CHECK(rx.try_recv() == true);
    CHECK(rx.try_recv() == true);
    CHECK(rx.try_recv() == false);
}

void test_mpmc_stress_no_loss_no_dup() {
    constexpr std::size_t cap = 1024;
    constexpr int producers = 4;
    constexpr int consumers = 4;
    constexpr int items_per_producer = 1000'000;
    constexpr int total = producers * items_per_producer;

    auto [tx, rx] = asco::concurrency::ring_queue::create<int, cap>();

    std::vector<std::atomic_uint8_t> seen(total);
    for (auto &s : seen) {
        s.store(0, std::memory_order_relaxed);
    }

    std::atomic_int produced{0};
    std::atomic_int consumed{0};
    std::atomic_bool dup{false};
    std::atomic_bool out_of_range{false};

    auto deadline = clock_type::now() + std::chrono::seconds(8);

    std::vector<std::thread> threads;
    threads.reserve(producers + consumers);

    for (int p = 0; p < producers; p++) {
        threads.emplace_back([&, p] {
            for (int i = 0; i < items_per_producer; i++) {
                int v = p * items_per_producer + i;
                push_blocking(tx, v, deadline);
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (int c = 0; c < consumers; c++) {
        threads.emplace_back([&] {
            while (consumed.load(std::memory_order_relaxed) < total) {
                auto r = rx.try_recv();
                if (!r.has_value()) {
                    if (clock_type::now() > deadline) {
                        fail("mpmc consumer timeout", __FILE__, __LINE__);
                    }
                    std::this_thread::yield();
                    continue;
                }
                int v = *r;
                if (v < 0 || v >= total) {
                    out_of_range.store(true, std::memory_order_relaxed);
                } else {
                    auto prev = seen[static_cast<std::size_t>(v)].exchange(1, std::memory_order_relaxed);
                    if (prev != 0) {
                        dup.store(true, std::memory_order_relaxed);
                    }
                }
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    CHECK(produced.load(std::memory_order_relaxed) == total);
    CHECK(consumed.load(std::memory_order_relaxed) == total);
    CHECK(!dup.load(std::memory_order_relaxed));
    CHECK(!out_of_range.load(std::memory_order_relaxed));
    for (int i = 0; i < total; i++) {
        if (seen[static_cast<std::size_t>(i)].load(std::memory_order_relaxed) != 1) {
            fail("missing item", __FILE__, __LINE__);
        }
    }
}

static std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch()).count());
}

void test_end_to_end_latency() {
    // 端到端延迟：生产线程写入时间戳，消费线程取出后计算 (pop_time - push_time)。
    // 不做硬阈值断言（容易在 CI/不同机器上波动），只确保流程正确并输出统计。

    struct msg {
        std::uint64_t t0_ns;
        std::uint32_t id;
    };

    constexpr std::size_t cap = 1024;
    constexpr std::uint32_t warmup = 10'000;
    constexpr std::uint32_t samples = 1000'000;

    auto [tx, rx] = asco::concurrency::ring_queue::create<msg, cap>();

    std::vector<std::uint64_t> lat_ns;
    lat_ns.reserve(samples);

    std::atomic_bool start{false};
    std::atomic_uint32_t produced{0};
    std::atomic_uint32_t consumed{0};

    auto deadline = clock_type::now() + std::chrono::seconds(10);

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
            if (clock_type::now() > deadline) {
                fail("latency producer start timeout", __FILE__, __LINE__);
            }
        }

        for (std::uint32_t i = 0; i < warmup + samples; i++) {
            msg m{now_ns(), i};
            push_blocking(tx, m, deadline);
            std::this_thread::yield();
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
            if (clock_type::now() > deadline) {
                fail("latency consumer start timeout", __FILE__, __LINE__);
            }
        }

        for (std::uint32_t i = 0; i < warmup + samples; i++) {
            auto m = pop_blocking(rx, deadline);
            auto t1 = now_ns();
            if (i >= warmup) {
                lat_ns.push_back(t1 - m.t0_ns);
            }
            // std::this_thread::yield();
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    CHECK(produced.load(std::memory_order_relaxed) == warmup + samples);
    CHECK(consumed.load(std::memory_order_relaxed) == warmup + samples);
    CHECK(lat_ns.size() == samples);

    auto lat_sorted = lat_ns;
    std::sort(lat_sorted.begin(), lat_sorted.end());

    auto pct = [&](double p) -> std::uint64_t {
        // p in [0, 100]
        double idx_f = (p / 100.0) * static_cast<double>(lat_sorted.size() - 1);
        std::size_t idx = static_cast<std::size_t>(idx_f);
        return lat_sorted[idx];
    };

    std::uint64_t min_v = lat_sorted.front();
    std::uint64_t max_v = lat_sorted.back();
    __uint128_t sum = 0;
    for (auto v : lat_ns) {
        sum += v;
    }
    auto avg = static_cast<std::uint64_t>(sum / lat_ns.size());

    std::println(
        "[ring_queue latency] samples={} avg={}ns min={}ns p50={}ns p90={}ns p99={}ns max={}ns",
        lat_ns.size(), avg, min_v, pct(50), pct(90), pct(99), max_v);
}

}  // namespace

int main() {
    try {
        test_basic_single_thread();
        test_capacity_full_empty();
        test_spsc_order_and_wrap();
        test_move_only_type();
        test_exception_path_recovers();
        test_void_specialization();
        test_end_to_end_latency();
        test_mpmc_stress_no_loss_no_dup();
    } catch (const test_failure &) { return 1; } catch (const std::exception &e) {
        std::println("[ring_queue test] UNCAUGHT EXCEPTION: {}", e.what());
        return 2;
    } catch (...) {
        std::println("[ring_queue test] UNCAUGHT UNKNOWN EXCEPTION");
        return 3;
    }

    std::println("[ring_queue test] OK");
    return 0;
}
