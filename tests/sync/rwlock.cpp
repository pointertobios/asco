// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <utility>

#include <asco/sync/rwlock.h>
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

ASCO_TEST(rwlock_void_try_read_try_write_and_raii_release) {
    sync::rwlock<> lock;

    auto r1 = lock.try_read();
    ASCO_CHECK(r1, "try_read() should succeed on an unlocked rwlock");

    auto r2 = lock.try_read();
    ASCO_CHECK(r2, "try_read() should allow multiple readers concurrently");

    auto w1 = lock.try_write();
    ASCO_CHECK(!w1, "try_write() should fail while readers are holding the rwlock");

    r1 = {};
    r2 = {};

    auto w2 = lock.try_write();
    ASCO_CHECK(w2, "try_write() should succeed after the last reader releases");
    ASCO_CHECK(!lock.try_read(), "try_read() should fail while a writer holds the rwlock");
    ASCO_CHECK(!lock.try_write(), "try_write() should fail while a writer already holds the rwlock");

    w2 = {};

    auto r3 = lock.try_read();
    ASCO_CHECK(r3, "try_read() should succeed again after the writer releases");

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_read_allows_multiple_readers) {
    sync::rwlock<> lock;

    std::atomic_bool second_entered{false};

    auto first = co_await lock.read();

    auto second = spawn([&]() -> future<void> {
        auto g = co_await lock.read();
        (void)g;
        second_entered.store(true, std::memory_order::release);
    });

    ASCO_CHECK(
        co_await wait([&]() { return second_entered.load(std::memory_order::acquire); }),
        "read() should allow another reader to enter while a reader is already holding the rwlock");

    co_await second;

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_write_waits_for_all_readers) {
    sync::rwlock<> lock;

    auto reader1 = co_await lock.read();
    auto reader2 = co_await lock.read();

    std::atomic_bool writer_entered{false};

    auto writer = spawn([&]() -> future<void> {
        auto g = co_await lock.write();
        (void)g;
        writer_entered.store(true, std::memory_order::release);
    });

    co_await yield_n(64);
    ASCO_CHECK(
        !writer_entered.load(std::memory_order::acquire), "write() should wait while readers are present");

    reader1 = {};

    co_await yield_n(64);
    ASCO_CHECK(
        !writer_entered.load(std::memory_order::acquire),
        "write() should still wait until the last active reader releases");

    reader2 = {};

    ASCO_CHECK(
        co_await wait([&]() { return writer_entered.load(std::memory_order::acquire); }),
        "writer should enter after every reader has released the rwlock");

    co_await writer;

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_upgrade_of_empty_guard_returns_empty_writer) {
    sync::rwlock<>::read_guard reader;

    auto writer = co_await std::move(reader).upgrade();

    ASCO_CHECK(!writer, "upgrade() on an empty read guard should return an empty write guard");

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_upgrade_transfers_last_reader_to_writer) {
    sync::rwlock<> lock;

    auto reader = co_await lock.read();

    auto upgrade = std::move(reader).upgrade();
    ASCO_CHECK(!reader, "upgrade() should consume the source read guard");

    auto writer = co_await upgrade;
    ASCO_CHECK(writer, "upgrade() should produce a valid write guard when this is the last reader");
    ASCO_CHECK(!lock.try_read(), "try_read() should fail while the upgraded writer holds the rwlock");
    ASCO_CHECK(!lock.try_write(), "try_write() should fail while the upgraded writer holds the rwlock");

    writer = {};

    auto reader2 = lock.try_read();
    ASCO_CHECK(
        reader2, "readers should be able to acquire the rwlock again after the upgraded writer releases");

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_upgrade_waits_for_other_readers_and_blocks_new_readers) {
    sync::rwlock<> lock;

    std::atomic_bool upgrade_entered{false};
    std::atomic_bool release_upgraded_writer{false};
    std::atomic_bool late_reader_entered{false};

    auto upgrader = co_await lock.read();
    auto other_reader = co_await lock.read();

    auto upgrade_task = spawn(
        [upgrader = std::move(upgrader), &upgrade_entered,
         &release_upgraded_writer]() mutable -> future<void> {
            auto writer = co_await std::move(upgrader).upgrade();
            (void)writer;
            upgrade_entered.store(true, std::memory_order::release);

            while (!release_upgraded_writer.load(std::memory_order::acquire)) {
                co_await this_task::yield();
            }
        });

    ASCO_CHECK(!upgrader, "moving a read guard into upgrade() should empty the original guard");

    co_await yield_n(64);
    ASCO_CHECK(
        !upgrade_entered.load(std::memory_order::acquire),
        "upgrade() should wait while another reader still holds the rwlock");
    ASCO_CHECK(
        co_await wait([&]() {
            auto g = lock.try_read();
            return !g;
        }),
        "try_read() should fail while a reader is waiting to upgrade");

    auto late_reader = spawn([&]() -> future<void> {
        auto g = co_await lock.read();
        (void)g;
        late_reader_entered.store(true, std::memory_order::release);
    });

    co_await yield_n(64);
    ASCO_CHECK(
        !late_reader_entered.load(std::memory_order::acquire),
        "new readers should not bypass a pending upgrade");

    other_reader = {};

    ASCO_CHECK(
        co_await wait([&]() { return upgrade_entered.load(std::memory_order::acquire); }),
        "upgrade() should complete after the other readers release");

    co_await yield_n(64);
    ASCO_CHECK(
        !late_reader_entered.load(std::memory_order::acquire),
        "an upgraded writer should continue excluding readers until it releases");

    release_upgraded_writer.store(true, std::memory_order::release);

    ASCO_CHECK(
        co_await wait([&]() { return late_reader_entered.load(std::memory_order::acquire); }),
        "readers queued behind an upgrade should proceed after the upgraded writer releases");

    co_await upgrade_task;
    co_await late_reader;

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_second_upgrade_fails_while_first_upgrade_is_pending) {
    sync::rwlock<> lock;

    std::atomic_bool first_upgrade_entered{false};
    std::atomic_bool release_first_writer{false};
    std::atomic_bool second_upgrade_finished{false};
    std::atomic_bool second_upgrade_succeeded{true};

    auto first_reader = co_await lock.read();
    auto second_reader = co_await lock.read();

    auto first_upgrade = spawn(
        [first_reader = std::move(first_reader), &first_upgrade_entered,
         &release_first_writer]() mutable -> future<void> {
            auto writer = co_await std::move(first_reader).upgrade();
            first_upgrade_entered.store(static_cast<bool>(writer), std::memory_order::release);

            while (!release_first_writer.load(std::memory_order::acquire)) {
                co_await this_task::yield();
            }
        });

    ASCO_CHECK(!first_reader, "moving a read guard into the first upgrade should empty the source guard");
    ASCO_CHECK(
        co_await wait([&]() {
            auto g = lock.try_read();
            return !g;
        }),
        "the first pending upgrade should block new readers before another upgrade starts");

    auto second_upgrade = spawn(
        [second_reader = std::move(second_reader), &second_upgrade_finished,
         &second_upgrade_succeeded]() mutable -> future<void> {
            auto writer = co_await std::move(second_reader).upgrade();
            second_upgrade_succeeded.store(static_cast<bool>(writer), std::memory_order::release);
            second_upgrade_finished.store(true, std::memory_order::release);
        });

    ASCO_CHECK(!second_reader, "moving a read guard into the second upgrade should empty the source guard");
    ASCO_CHECK(
        co_await wait([&]() { return second_upgrade_finished.load(std::memory_order::acquire); }),
        "a concurrent second upgrade should finish promptly instead of waiting indefinitely");
    ASCO_CHECK(
        !second_upgrade_succeeded.load(std::memory_order::acquire),
        "the second concurrent upgrade should fail and return an empty write guard");

    ASCO_CHECK(
        co_await wait([&]() { return first_upgrade_entered.load(std::memory_order::acquire); }),
        "the first upgrade should complete after the failed second upgrade releases its reader share");

    release_first_writer.store(true, std::memory_order::release);

    co_await first_upgrade;
    co_await second_upgrade;

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_void_waiting_writer_blocks_new_readers) {
    sync::rwlock<> lock;

    std::atomic_size_t acquire_order{0};
    std::atomic_size_t writer_order{0};
    std::atomic_size_t reader_order{0};

    auto first_reader = co_await lock.read();

    auto writer = spawn([&]() -> future<void> {
        auto g = co_await lock.write();
        (void)g;
        writer_order.store(
            acquire_order.fetch_add(1, std::memory_order::acq_rel) + 1, std::memory_order::release);
    });

    ASCO_CHECK(
        co_await wait([&]() {
            auto g = lock.try_read();
            return !g;
        }),
        "try_read() should fail once a writer is queued behind an existing reader");

    auto late_reader = spawn([&]() -> future<void> {
        auto g = co_await lock.read();
        (void)g;
        reader_order.store(
            acquire_order.fetch_add(1, std::memory_order::acq_rel) + 1, std::memory_order::release);
    });

    co_await yield_n(64);
    ASCO_CHECK(
        reader_order.load(std::memory_order::acquire) == 0, "new readers should not bypass a queued writer");

    first_reader = {};

    ASCO_CHECK(
        co_await wait([&]() { return writer_order.load(std::memory_order::acquire) != 0; }),
        "queued writer should eventually acquire the rwlock after existing readers leave");
    ASCO_CHECK(
        co_await wait([&]() { return reader_order.load(std::memory_order::acquire) != 0; }),
        "reader queued after a writer should acquire the rwlock after the writer releases");
    ASCO_CHECK(
        writer_order.load(std::memory_order::acquire) < reader_order.load(std::memory_order::acquire),
        "queued writer should acquire the rwlock before later readers");

    co_await writer;
    co_await late_reader;

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_t_read_and_write_guards_access_bound_value) {
    sync::rwlock<int> lock{41};

    {
        auto g = co_await lock.read();
        ASCO_CHECK(*g == 41, "read guard should expose the initial value");
    }

    {
        auto g = co_await lock.write();
        ASCO_CHECK(*g == 41, "write guard should observe the current value");
        *g = 42;
        ASCO_CHECK(*g == 42, "write guard should allow mutating the protected value");
    }

    {
        auto g = co_await lock.read();
        ASCO_CHECK(*g == 42, "writes should remain visible after the writer releases");
    }

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_t_upgrade_allows_mutating_bound_value) {
    sync::rwlock<int> lock{41};

    auto reader = co_await lock.read();
    ASCO_CHECK(*reader == 41, "read guard should expose the current value before upgrade()");

    auto writer = co_await std::move(reader).upgrade();
    ASCO_CHECK(!reader, "upgrade() should consume rwlock<T>::read_guard");
    ASCO_CHECK(writer, "upgrade() should produce rwlock<T>::write_guard");

    *writer = 42;

    writer = {};

    auto reader2 = co_await lock.read();
    ASCO_CHECK(*reader2 == 42, "mutation through an upgraded writer guard should persist after release");

    ASCO_SUCCESS();
}

ASCO_TEST(rwlock_t_guard_moves_clear_source_and_try_methods_reflect_state) {
    sync::rwlock<int> lock{7};

    auto writer1 = co_await lock.write();
    ASCO_CHECK(writer1, "write() should return a valid write guard");

    auto writer2 = std::move(writer1);
    ASCO_CHECK(!writer1, "moved-from write guard should be empty");
    ASCO_CHECK(writer2, "moved-to write guard should remain valid");

    *writer2 = 8;
    ASCO_CHECK(!lock.try_read(), "try_read() should fail while a write guard is held");
    ASCO_CHECK(!lock.try_write(), "try_write() should fail while a write guard is held");

    writer2 = {};

    auto reader1 = lock.try_read();
    ASCO_CHECK(reader1, "try_read() should succeed after the writer releases");
    ASCO_CHECK(*reader1 == 8, "reader should observe the value written by the previous writer");

    auto reader2 = std::move(reader1);
    ASCO_CHECK(!reader1, "moved-from read guard should be empty");
    ASCO_CHECK(reader2, "moved-to read guard should remain valid");
    ASCO_CHECK(*reader2 == 8, "moved-to read guard should keep access to the protected value");

    ASCO_SUCCESS();
}