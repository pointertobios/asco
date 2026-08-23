// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/sync/condition_variable.h"

#include <array>
#include <atomic>
#include <type_traits>
#include <vector>

#include "asco/future.h"
#include "asco/join_handle.h"
#include "asco/test/test.h"
#include "asco/this_task.h"

// condition_variable 不可拷贝、不可移动
static_assert(!std::is_copy_constructible_v<asco::sync::condition_variable>);
static_assert(!std::is_copy_assignable_v<asco::sync::condition_variable>);
static_assert(!std::is_move_constructible_v<asco::sync::condition_variable>);
static_assert(!std::is_move_assignable_v<asco::sync::condition_variable>);

// 无等待者时：notify_one 返回 false，notify_all 返回 0
ASCO_TEST(notify_without_waiter) {
    asco::sync::condition_variable cv{};
    ASCO_CHECK(!cv.notify_one(), "无等待者时 notify_one() 应返回 false");
    ASCO_CHECK(cv.notify_all() == 0, "无等待者时 notify_all() 应返回 0");
    ASCO_SUCCESS();
}

// 无谓词等待：等待者挂起后被 notify_one 唤醒
ASCO_TEST(notify_one_wakes_waiter) {
    asco::sync::condition_variable cv{};
    std::atomic<bool> woken{false};

    auto jh = asco::this_task::spawn([&]() -> asco::future<> {
        co_await cv();
        woken.store(true, std::memory_order::release);
    });

    // 等待者尚未挂起时 notify_one 返回 false，轮询直到成功唤醒
    while (!cv.notify_one()) {
        co_await asco::this_task::yield();
    }

    co_await jh;
    ASCO_CHECK(woken.load(std::memory_order::acquire), "等待者应被 notify_one 唤醒");
    ASCO_SUCCESS();
}

// notify_all 唤醒全部等待者并返回等待者数量
ASCO_TEST(notify_all_wakes_all) {
    constexpr asco::usize kN = 8;
    asco::sync::condition_variable cv{};
    std::array<std::atomic<bool>, kN> started{};
    std::atomic<asco::usize> woken{0};

    std::vector<asco::join_handle<void>> jhs;
    jhs.reserve(kN);
    for (asco::usize i = 0; i < kN; ++i) {
        jhs.emplace_back(asco::this_task::spawn([&, i]() -> asco::future<> {
            started[i].store(true, std::memory_order::release);
            co_await cv();
            woken.fetch_add(1, std::memory_order::acq_rel);
        }));
    }

    // 等待所有等待者开始执行，并给调度器机会完成挂起
    for (asco::usize i = 0; i < kN; ++i) {
        while (!started[i].load(std::memory_order::acquire)) {
            co_await asco::this_task::yield();
        }
    }
    for (int i = 0; i < 8; ++i) {
        co_await asco::this_task::yield();
    }

    asco::usize n = cv.notify_all();
    ASCO_CHECK(n == kN, "notify_all 应返回等待者数量，期望 {} 实际 {}", kN, n);

    for (auto &jh : jhs) {
        co_await jh;
    }
    ASCO_CHECK(woken.load(std::memory_order::acquire) == kN, "所有等待者都应被唤醒，期望 {} 实际 {}",
               kN, woken.load(std::memory_order::acquire));
    ASCO_SUCCESS();
}

// 谓词立即满足：不挂起，谓词只求值一次
ASCO_TEST(predicate_immediate) {
    asco::sync::condition_variable cv{};
    int calls = 0;
    co_await cv([&]() -> bool {
        ++calls;
        return true;
    });
    ASCO_CHECK(calls == 1, "谓词立即满足时应只求值一次，实际 {}", calls);
    ASCO_SUCCESS();
}

// 谓词驱动等待：初始不满足则挂起，置真并通知后退出
ASCO_TEST(predicate_wake_after_notify) {
    asco::sync::condition_variable cv{};
    std::atomic<bool> ready{false};
    std::atomic<bool> exited{false};

    auto jh = asco::this_task::spawn([&]() -> asco::future<> {
        co_await cv([&] { return ready.load(std::memory_order::acquire); });
        exited.store(true, std::memory_order::release);
    });

    ready.store(true, std::memory_order::release);
    cv.notify_one();  // 等待者已挂起则唤醒；否则由谓词兜底
    co_await jh;
    ASCO_CHECK(exited.load(std::memory_order::acquire), "谓词满足后等待者应退出等待");
    ASCO_SUCCESS();
}

// 并发正确性：多轮 notify_all 与谓词重查，无丢失唤醒
ASCO_TEST(concurrent_round_trip) {
    constexpr asco::usize kWaiters = 16;
    constexpr asco::usize kRounds = 64;
    asco::sync::condition_variable cv{};
    std::atomic<asco::usize> round{0};
    std::atomic<asco::usize> done{0};

    auto waiter = [&]() -> asco::future<> {
        for (asco::usize i = 0; i < kRounds; ++i) {
            co_await cv([&, i] { return round.load(std::memory_order::acquire) == i + 1; });
            done.fetch_add(1, std::memory_order::acq_rel);
        }
    };

    std::vector<asco::join_handle<void>> jhs;
    jhs.reserve(kWaiters);
    for (asco::usize i = 0; i < kWaiters; ++i) {
        jhs.emplace_back(asco::this_task::spawn(waiter));
    }

    for (asco::usize i = 0; i < kRounds; ++i) {
        round.store(i + 1, std::memory_order::release);
        cv.notify_all();
        asco::usize target = (i + 1) * kWaiters;
        while (done.load(std::memory_order::acquire) < target) {
            co_await asco::this_task::yield();
        }
    }

    for (auto &jh : jhs) {
        co_await jh;
    }
    ASCO_CHECK(done.load(std::memory_order::acquire) == kRounds * kWaiters,
               "每轮每个等待者都应被唤醒一次，期望 {} 实际 {}", kRounds * kWaiters,
               done.load(std::memory_order::acquire));
    ASCO_SUCCESS();
}

// 并发正确性：多次 notify_one 逐个唤醒等待者（容忍虚假唤醒，循环通知直到退出）
#if 0
ASCO_TEST(concurrent_notify_one_each) {
    constexpr asco::usize kN = 8;
    asco::sync::condition_variable cv{};
    std::array<std::atomic<bool>, kN> go{};
    std::atomic<asco::usize> woken{0};

    std::vector<asco::join_handle<void>> jhs;
    jhs.reserve(kN);
    for (asco::usize i = 0; i < kN; ++i) {
        jhs.emplace_back(asco::this_task::spawn([&, i]() -> asco::future<> {
            co_await cv([&, i] { return go[i].load(std::memory_order::acquire); });
            woken.fetch_add(1, std::memory_order::acq_rel);
        }));
    }

    // 逐个置位并通知，直到对应等待者退出；等待者未挂起或虚假唤醒时循环重试
    for (asco::usize i = 0; i < kN; ++i) {
        go[i].store(true, std::memory_order::release);
        asco::usize target = i + 1;
        while (woken.load(std::memory_order::acquire) < target) {
            cv.notify_one();
            co_await asco::this_task::yield();
        }
    }

    for (auto &jh : jhs) {
        co_await jh;
    }
    ASCO_CHECK(woken.load(std::memory_order::acquire) == kN,
               "N 次 notify_one 应恰好唤醒 N 个等待者，期望 {} 实际 {}", kN,
               woken.load(std::memory_order::acquire));
    ASCO_SUCCESS();
}
#endif
