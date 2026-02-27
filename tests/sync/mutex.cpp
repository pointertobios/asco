// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
#include <functional>
#include <utility>

#include <asco/sync/mutex.h>
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
future<bool> wait(Pred &&pred) {
    while (true) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await this_task::yield();
    }
}

}  // namespace

ASCO_TEST(mutex_void_try_lock_and_raii_release) {
    sync::mutex<> m;

    auto g1 = m.try_lock();
    ASCO_CHECK(g1, "try_lock() should succeed initially");

    auto g2 = m.try_lock();
    ASCO_CHECK(!g2, "try_lock() should fail when already locked");

    g1 = {};

    auto g3 = m.try_lock();
    ASCO_CHECK(g3, "try_lock() should succeed after guard releases");

    ASCO_SUCCESS();
}

ASCO_TEST(mutex_void_lock_blocks_and_release_wakes) {
    sync::mutex<> m;

    std::atomic_bool entered{false};

    {
        auto holder = co_await m.lock();

        auto waiter = spawn([&]() -> future<void> {
            auto g = co_await m.lock();
            (void)g;
            entered.store(true, std::memory_order::release);
        });

        co_await yield_n(64);
        ASCO_CHECK(!entered.load(std::memory_order::acquire), "lock() should block while mutex is held");

        holder = {};

        ASCO_CHECK(
            co_await wait([&]() { return entered.load(std::memory_order::acquire); }),
            "waiter should enter after mutex is released");

        co_await waiter;
    }

    ASCO_SUCCESS();
}

ASCO_TEST(mutex_t_lock_allows_access_and_mutation) {
    sync::mutex<int> m{41};

    {
        auto g = co_await m.lock();
        ASCO_CHECK(*g == 41, "initial value should be 41");
        *g = 42;
        ASCO_CHECK(*g == 42, "value should be mutable through guard");
    }

    {
        auto g = co_await m.lock();
        ASCO_CHECK(*g == 42, "mutation should persist after unlock");
    }

    ASCO_SUCCESS();
}

ASCO_TEST(mutex_t_guard_move_clears_source) {
    sync::mutex<int> m{1};

    auto g1 = co_await m.lock();
    ASCO_CHECK(g1, "guard should be truthy after lock()");

    auto g2 = std::move(g1);
    ASCO_CHECK(!g1, "moved-from guard should be empty");
    ASCO_CHECK(g2, "moved-to guard should be valid");

    *g2 = 7;
    ASCO_CHECK(*g2 == 7, "moved-to guard should still protect the value");

    ASCO_SUCCESS();
}

ASCO_TEST(mutex_t_try_lock_fails_while_held) {
    sync::mutex<int> m{0};

    auto holder = co_await m.lock();

    auto g = m.try_lock();
    ASCO_CHECK(!g, "try_lock() should fail when already locked");

    holder = {};

    auto g2 = m.try_lock();
    ASCO_CHECK(g2, "try_lock() should succeed after unlock");

    ASCO_SUCCESS();
}
