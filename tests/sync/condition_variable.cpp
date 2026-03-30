// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <vector>

#include <asco/sync/condition_variable.h>
#include <asco/test/test.h>
#include <asco/yield.h>

using namespace asco;

namespace {

future<void> yield_n(std::size_t n) {
    for (std::size_t i = 0; i < n; i++) {
        co_await this_task::yield();
    }
}

template<typename Pred>
future<bool> wait(Pred &&pred, std::chrono::steady_clock::duration max_wait = std::chrono::seconds{2}) {
    const auto deadline = std::chrono::steady_clock::now() + max_wait;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await this_task::yield();
    }
    co_return std::invoke(pred);
}

}  // namespace

ASCO_TEST(condition_variable_notify_reports_no_waiters_when_empty) {
    sync::condition_variable cv;

    ASCO_CHECK(!cv.notify_one(), "notify_one() should return false when there are no waiters");
    ASCO_CHECK(cv.notify() == 0, "notify() should return 0 when there are no waiters");
    ASCO_CHECK(cv.notify(3) == 0, "notify(3) should return 0 when there are no waiters");

    auto one = cv.notify_one([]() { return true; });
    ASCO_CHECK(!one.has_value(), "predicated notify_one() should fail when there are no waiters");
    ASCO_CHECK(
        one.error() == sync::notify_failed::no_waiters,
        "predicated notify_one() should report no_waiters on an empty queue");

    auto all = cv.notify([]() { return true; });
    ASCO_CHECK(!all.has_value(), "predicated notify() should fail when there are no waiters");
    ASCO_CHECK(
        all.error() == sync::notify_failed::no_waiters,
        "predicated notify() should report no_waiters on an empty queue");

    auto many = cv.notify([]() { return true; }, 3);
    ASCO_CHECK(!many.has_value(), "predicated notify(n) should fail when there are no waiters");
    ASCO_CHECK(
        many.error() == sync::notify_failed::no_waiters,
        "predicated notify(n) should report no_waiters on an empty queue");

    ASCO_SUCCESS();
}

ASCO_TEST(condition_variable_wait_returns_immediately_when_predicate_true) {
    sync::condition_variable cv;
    std::atomic_size_t calls{0};

    co_await cv.wait([&]() {
        calls.fetch_add(1, std::memory_order::acq_rel);
        return true;
    });

    ASCO_CHECK(
        calls.load(std::memory_order::acquire) == 1,
        "wait() should evaluate predicate exactly once when it is already true");

    ASCO_SUCCESS();
}

ASCO_TEST(condition_variable_wait_blocks_until_notify_after_predicate_changes) {
    sync::condition_variable cv;

    std::atomic_bool ready{false};
    std::atomic_bool passed{false};
    std::atomic_size_t calls{0};

    auto waiter = spawn([&]() -> future<void> {
        co_await cv.wait([&]() {
            calls.fetch_add(1, std::memory_order::acq_rel);
            return ready.load(std::memory_order::acquire);
        });
        passed.store(true, std::memory_order::release);
    });

    ASCO_CHECK(
        co_await wait([&]() { return calls.load(std::memory_order::acquire) >= 1; }),
        "waiter should evaluate predicate before blocking");

    co_await yield_n(64);
    ASCO_CHECK(
        !passed.load(std::memory_order::acquire),
        "wait() should remain blocked while predicate is false and no notification is sent");

    ready.store(true, std::memory_order::release);
    ASCO_CHECK(cv.notify_one(), "notify_one() should report one waiting task");

    ASCO_CHECK(
        co_await wait([&]() { return passed.load(std::memory_order::acquire); }),
        "wait() should resume after predicate becomes true and a waiter is notified");

    co_await waiter;

    ASCO_CHECK(
        calls.load(std::memory_order::acquire) >= 2,
        "wait() should re-evaluate predicate after being notified");

    ASCO_SUCCESS();
}

ASCO_TEST(condition_variable_wait_once_returns_false_when_predicate_true) {
    sync::condition_variable cv;
    std::atomic_size_t calls{0};

    auto suspended = co_await cv.wait_once([&]() {
        calls.fetch_add(1, std::memory_order::acq_rel);
        return true;
    });

    ASCO_CHECK(!suspended, "wait_once() should return false when predicate is already true");
    ASCO_CHECK(
        calls.load(std::memory_order::acquire) == 1, "wait_once() should evaluate predicate exactly once");
    ASCO_CHECK(!cv.notify_one(), "wait_once() should not enqueue when predicate is already true");

    ASCO_SUCCESS();
}

ASCO_TEST(condition_variable_wait_once_suspends_once_and_returns_true_after_notify) {
    sync::condition_variable cv;

    std::atomic_bool ready{false};
    std::atomic_bool suspended{false};
    std::atomic_bool resumed{false};
    std::atomic_size_t calls{0};

    auto waiter = spawn([&]() -> future<void> {
        suspended.store(
            co_await cv.wait_once([&]() {
                calls.fetch_add(1, std::memory_order::acq_rel);
                return ready.load(std::memory_order::acquire);
            }),
            std::memory_order::release);
        resumed.store(true, std::memory_order::release);
    });

    ASCO_CHECK(
        co_await wait([&]() { return calls.load(std::memory_order::acquire) == 1; }),
        "wait_once() should evaluate predicate before blocking");

    co_await yield_n(64);
    ASCO_CHECK(
        !resumed.load(std::memory_order::acquire), "wait_once() should block while predicate is false");

    ready.store(true, std::memory_order::release);
    ASCO_CHECK(cv.notify_one(), "notify_one() should wake one wait_once() waiter");

    ASCO_CHECK(
        co_await wait([&]() { return resumed.load(std::memory_order::acquire); }),
        "wait_once() should resume after one notification");

    co_await waiter;

    ASCO_CHECK(suspended.load(std::memory_order::acquire), "wait_once() should report that it suspended");
    ASCO_CHECK(
        calls.load(std::memory_order::acquire) == 1,
        "wait_once() should not re-evaluate predicate after wakeup");

    ASCO_SUCCESS();
}

ASCO_TEST(condition_variable_predicated_notify_one_respects_predicate) {
    sync::condition_variable cv;

    std::atomic_bool suspended{false};
    std::atomic_bool resumed{false};
    std::atomic_size_t predicate_calls{0};

    auto waiter = spawn([&]() -> future<void> {
        suspended.store(
            co_await cv.wait_once([&]() {
                predicate_calls.fetch_add(1, std::memory_order::acq_rel);
                return false;
            }),
            std::memory_order::release);
        resumed.store(true, std::memory_order::release);
    });

    ASCO_CHECK(
        co_await wait([&]() { return predicate_calls.load(std::memory_order::acquire) == 1; }),
        "waiter should evaluate predicate and enqueue before predicated notify_one()");

    auto rejected = cv.notify_one([]() { return false; });
    ASCO_CHECK(!rejected.has_value(), "notify_one(predicate) should fail when predicate returns false");
    ASCO_CHECK(
        rejected.error() == sync::notify_failed::predicate_false,
        "notify_one(predicate) should report predicate_false when predicate rejects notification");

    co_await yield_n(64);
    ASCO_CHECK(
        !resumed.load(std::memory_order::acquire), "notify_one(predicate=false) should not wake the waiter");

    auto accepted = cv.notify_one([]() { return true; });
    ASCO_CHECK(accepted.has_value(), "notify_one(predicate=true) should wake the waiter");

    ASCO_CHECK(
        co_await wait([&]() { return resumed.load(std::memory_order::acquire); }),
        "notify_one(predicate=true) should eventually resume the waiter");

    co_await waiter;

    ASCO_CHECK(suspended.load(std::memory_order::acquire), "waiter should have suspended exactly once");

    ASCO_SUCCESS();
}

ASCO_TEST(condition_variable_predicated_notify_wakes_at_most_n_waiters) {
    static constexpr std::size_t waiter_count = 3;

    sync::condition_variable cv;

    std::atomic_size_t predicate_calls{0};
    std::atomic_size_t suspended_count{0};
    std::atomic_size_t resumed_count{0};

    std::vector<join_handle<void>> waiters;
    waiters.reserve(waiter_count);

    for (std::size_t i = 0; i < waiter_count; i++) {
        waiters.emplace_back(spawn([&]() -> future<void> {
            if (co_await cv.wait_once([&]() {
                    predicate_calls.fetch_add(1, std::memory_order::acq_rel);
                    return false;
                })) {
                suspended_count.fetch_add(1, std::memory_order::acq_rel);
            }
            resumed_count.fetch_add(1, std::memory_order::acq_rel);
        }));
    }

    ASCO_CHECK(
        co_await wait([&]() { return predicate_calls.load(std::memory_order::acquire) == waiter_count; }),
        "all waiters should evaluate predicate and enqueue before predicated notify()");

    auto rejected = cv.notify([]() { return false; }, 2);
    ASCO_CHECK(!rejected.has_value(), "notify(predicate, n) should fail when predicate returns false");
    ASCO_CHECK(
        rejected.error() == sync::notify_failed::predicate_false,
        "notify(predicate, n) should report predicate_false when predicate rejects notification");

    co_await yield_n(64);
    ASCO_CHECK(
        resumed_count.load(std::memory_order::acquire) == 0,
        "notify(predicate=false, n) should not wake any waiter");

    auto two = cv.notify([]() { return true; }, 2);
    ASCO_CHECK(two.has_value(), "notify(predicate=true, 2) should succeed");
    ASCO_CHECK(two.value() == 2, "notify(predicate=true, 2) should report waking two waiters");
    ASCO_CHECK(
        co_await wait([&]() { return resumed_count.load(std::memory_order::acquire) >= 2; }),
        "notify(predicate=true, 2) should wake two waiters");

    co_await yield_n(64);
    ASCO_CHECK(
        resumed_count.load(std::memory_order::acquire) == 2,
        "notify(predicate=true, 2) should not wake more than two waiters");

    auto remaining = cv.notify([]() { return true; });
    ASCO_CHECK(remaining.has_value(), "notify(predicate=true) should wake remaining waiters");
    ASCO_CHECK(remaining.value() == 1, "notify(predicate=true) should report the remaining waiter count");
    ASCO_CHECK(
        co_await wait([&]() { return resumed_count.load(std::memory_order::acquire) == waiter_count; }),
        "the final waiter should resume after notify(predicate=true)");

    for (auto &waiter : waiters) {
        co_await waiter;
    }

    ASCO_CHECK(
        suspended_count.load(std::memory_order::acquire) == waiter_count,
        "all waiters should have actually suspended before being resumed");

    ASCO_SUCCESS();
}
