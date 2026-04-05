// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
#include <vector>

#include "../async_test_utils.h"

#include <asco/panic.h>
#include <asco/sync/semaphore.h>
#include <asco/test/test.h>

using namespace asco;

ASCO_TEST(semaphore_basic_try_acquire_release) {
    sync::semaphore<3> sem{2};

    ASCO_CHECK(sem.try_acquire(), "try_acquire #1 should succeed");
    ASCO_CHECK(sem.try_acquire(), "try_acquire #2 should succeed");
    ASCO_CHECK(!sem.try_acquire(), "try_acquire #3 should fail when count is 0");

    sem.release(1);
    ASCO_CHECK(sem.try_acquire(), "try_acquire should succeed after release(1)");
    ASCO_CHECK(!sem.try_acquire(), "try_acquire should fail again after consuming last token");

    sem.release(100);
    ASCO_CHECK(sem.try_acquire(), "release saturates: acquire #1 should succeed");
    ASCO_CHECK(sem.try_acquire(), "release saturates: acquire #2 should succeed");
    ASCO_CHECK(sem.try_acquire(), "release saturates: acquire #3 should succeed");
    ASCO_CHECK(!sem.try_acquire(), "release saturates: acquire #4 should fail (N=3)");

    ASCO_SUCCESS();
}

ASCO_TEST(semaphore_count_and_release_return_value) {
    sync::semaphore<3> sem{0};

    ASCO_CHECK(sem.get_count() == 0, "initial count should be 0");

    ASCO_CHECK(sem.release(0) == 0, "release(0) should add 0 permits");
    ASCO_CHECK(sem.get_count() == 0, "count should remain 0 after release(0)");

    ASCO_CHECK(sem.release(10) == 3, "release saturates to N and returns actual increment");
    ASCO_CHECK(sem.get_count() == 3, "count should saturate to N");

    ASCO_CHECK(sem.release(1) == 0, "release on full semaphore should return 0");
    ASCO_CHECK(sem.get_count() == 3, "count should stay at N when full");

    ASCO_CHECK(sem.try_acquire(), "try_acquire should succeed when count > 0");
    ASCO_CHECK(sem.get_count() == 2, "count should decrement after successful try_acquire");

    ASCO_CHECK(sem.release(2) == 1, "release should only fill up to N");
    ASCO_CHECK(sem.get_count() == 3, "count should return to N after release");

    sync::semaphore<3> sem2{100};
    ASCO_CHECK(sem2.get_count() == 3, "constructor should clamp initial count to N");

    ASCO_SUCCESS();
}

ASCO_TEST(semaphore_acquire_blocks_and_release_wakes) {
    sync::binary_semaphore sem{0};

    std::atomic_bool attempted{false};
    std::atomic_bool passed{false};
    auto waiter = spawn([&]() -> future<void> {
        attempted.store(true, std::memory_order::release);
        co_await sem.acquire();
        passed.store(true, std::memory_order::release);
    });

    ASCO_CHECK(
        co_await test::wait_until([&]() { return attempted.load(std::memory_order::acquire); }),
        "waiter did not attempt acquire() in time");
    ASCO_CHECK(
        co_await test::stays_false_for([&]() { return passed.load(std::memory_order::acquire); }),
        "acquire() should block when count == 0");

    sem.release(1);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return passed.load(std::memory_order::acquire); }),
        "release(1) should wake one waiter");

    co_await waiter;

    ASCO_SUCCESS();
}

ASCO_TEST(semaphore_release_wakes_at_most_n_waiters) {
    sync::semaphore<2> sem{0};

    std::atomic_size_t attempted{0};
    std::atomic_size_t passed{0};

    auto w1 = spawn([&]() -> future<void> {
        attempted.fetch_add(1, std::memory_order::acq_rel);
        co_await sem.acquire();
        passed.fetch_add(1, std::memory_order::acq_rel);
    });
    auto w2 = spawn([&]() -> future<void> {
        attempted.fetch_add(1, std::memory_order::acq_rel);
        co_await sem.acquire();
        passed.fetch_add(1, std::memory_order::acq_rel);
    });

    ASCO_CHECK(
        co_await test::wait_until([&]() { return attempted.load(std::memory_order::acquire) == 2; }),
        "both waiters should attempt acquire() before release()");
    ASCO_CHECK(
        co_await test::stays_false_for([&]() { return passed.load(std::memory_order::acquire) > 0; }),
        "both waiters should be blocked initially");

    sem.release(1);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return passed.load(std::memory_order::acquire) >= 1; }),
        "release(1) should wake one waiter");
    ASCO_CHECK(
        passed.load(std::memory_order::acquire) == 1, "release(1) should not wake more than one waiter");

    sem.release(1);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return passed.load(std::memory_order::acquire) == 2; }),
        "second release(1) should wake the other waiter");

    co_await w1;
    co_await w2;

    ASCO_SUCCESS();
}

ASCO_TEST(semaphore_blocking_acquire_panics_in_runtime) {
    sync::binary_semaphore sem{1};

    bool panicked_now = false;
    try {
        sem.blocking_acquire();
    } catch (asco::panicked &) { panicked_now = true; }

    ASCO_CHECK(panicked_now, "blocking_acquire() should panic when called inside runtime");

    ASCO_SUCCESS();
}

ASCO_TEST(semaphore_concurrency_correctness) {
    static constexpr std::size_t tasks = 16;

    sync::semaphore<tasks> sem{0};
    sync::unlimited_semaphore gate{0};

    std::atomic_size_t entered{0};
    std::atomic_size_t finished{0};

    std::vector<join_handle<void>> handles;
    handles.reserve(tasks);

    for (std::size_t i = 0; i < tasks; i++) {
        handles.emplace_back(spawn([&]() -> future<void> {
            co_await sem.acquire();
            entered.fetch_add(1, std::memory_order::acq_rel);
            co_await gate.acquire();
            finished.fetch_add(1, std::memory_order::acq_rel);
        }));
    }

    std::size_t released = 0;

    released += 5;
    sem.release(5);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return entered.load(std::memory_order::acquire) == released; }),
        "after release(5), exactly 5 tasks should pass acquire()");
    ASCO_CHECK(
        entered.load(std::memory_order::acquire) <= released, "entered should never exceed released permits");

    released += 4;
    sem.release(4);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return entered.load(std::memory_order::acquire) == released; }),
        "after release(4), exactly 4 more tasks should pass acquire()");
    ASCO_CHECK(
        entered.load(std::memory_order::acquire) <= released, "entered should never exceed released permits");

    released += 7;
    sem.release(7);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return entered.load(std::memory_order::acquire) == released; }),
        "after release(7), exactly 7 more tasks should pass acquire()");
    ASCO_CHECK(
        entered.load(std::memory_order::acquire) <= released, "entered should never exceed released permits");

    ASCO_CHECK(released == tasks, "released total should equal tasks");
    ASCO_CHECK(
        entered.load(std::memory_order::acquire) == tasks,
        "all tasks should have passed acquire() after releasing enough permits");

    gate.release(tasks);
    ASCO_CHECK(
        co_await test::wait_until([&]() { return finished.load(std::memory_order::acquire) == tasks; }),
        "all tasks should complete after gate opens");

    for (auto &h : handles) {
        co_await h;
    }

    ASCO_SUCCESS();
}
