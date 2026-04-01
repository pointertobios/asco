// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>

#include <asco/sync/channel.h>
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

ASCO_TEST(channel_recv_blocks_until_value_is_sent) {
    auto [tx, rx] = sync::channel<int>();

    std::atomic_bool resumed{false};
    std::optional<int> received;

    auto waiter = spawn([&]() -> future<void> {
        auto value = co_await rx.recv();
        if (value) {
            received = *value;
        }
        resumed.store(true, std::memory_order::release);
    });

    co_await yield_n(64);
    ASCO_CHECK(!resumed.load(std::memory_order::acquire), "recv() should block while the channel is empty");

    auto sent = co_await tx.send(42);
    ASCO_CHECK(sent.has_value(), "send() should succeed for an open channel");

    ASCO_CHECK(
        co_await wait([&]() { return resumed.load(std::memory_order::acquire); }),
        "recv() should resume after a value is sent");

    co_await waiter;

    ASCO_CHECK(received.has_value() && *received == 42, "recv() should return the value sent by sender");

    ASCO_SUCCESS();
}

ASCO_TEST(channel_send_blocks_when_full_and_recv_releases_backpressure) {
    constexpr std::size_t cap = sync::detail::channel_capacity;

    auto [tx, rx] = sync::channel<std::size_t>();

    for (std::size_t i = 0; i < cap; i++) {
        auto sent = co_await tx.send(i);
        ASCO_CHECK(sent.has_value(), "prefill send #{} should succeed", i);
    }

    std::atomic_bool extra_completed{false};
    std::atomic_bool extra_succeeded{false};

    auto blocked_sender = spawn([&]() -> future<void> {
        auto sent = co_await tx.send(cap);
        extra_succeeded.store(sent.has_value(), std::memory_order::release);
        extra_completed.store(true, std::memory_order::release);
    });

    co_await yield_n(64);
    ASCO_CHECK(
        !extra_completed.load(std::memory_order::acquire), "send() should suspend when channel capacity is full");

    auto first = co_await rx.recv();
    ASCO_CHECK(first.has_value() && *first == 0, "recv() should consume the oldest queued element first");

    ASCO_CHECK(
        co_await wait([&]() { return extra_completed.load(std::memory_order::acquire); }),
        "consuming one value should release exactly one blocked sender");

    co_await blocked_sender;

    ASCO_CHECK(
        extra_succeeded.load(std::memory_order::acquire), "the blocked send should complete successfully");

    for (std::size_t expected = 1; expected <= cap; expected++) {
        auto value = co_await rx.recv();
        ASCO_CHECK(value.has_value(), "recv() should return queued value #{}", expected);
        ASCO_CHECK(*value == expected, "channel should preserve FIFO order at index {}", expected);
    }

    ASCO_SUCCESS();
}

ASCO_TEST(channel_sender_stop_drains_buffer_then_closes) {
    auto [tx, rx] = sync::channel<int>();

    ASCO_CHECK((co_await tx.send(7)).has_value(), "first send() should succeed");
    ASCO_CHECK((co_await tx.send(8)).has_value(), "second send() should succeed");
    ASCO_CHECK((co_await tx.send(9)).has_value(), "third send() should succeed");

    tx.stop();

    auto first = co_await rx.recv();
    ASCO_CHECK(first.has_value() && *first == 7, "after sender.stop(), first buffered value should still be receivable");

    auto second = co_await rx.recv();
    ASCO_CHECK(
        second.has_value() && *second == 8, "after sender.stop(), second buffered value should still be receivable");

    auto third = co_await rx.recv();
    ASCO_CHECK(third.has_value() && *third == 9, "after sender.stop(), last buffered value should still be receivable");

    auto closed = co_await rx.recv();
    ASCO_CHECK(!closed, "recv() should report channel closed after draining buffered values");

    auto rejected = co_await tx.send(10);
    ASCO_CHECK(!rejected.has_value(), "send() should fail after sender.stop()");
    ASCO_CHECK(rejected.error() == 10, "failed send() should return the rejected value");

    ASCO_SUCCESS();
}

ASCO_TEST(channel_receiver_stop_drains_buffer_and_rejects_future_sends) {
    auto [tx, rx] = sync::channel<int>();

    ASCO_CHECK((co_await tx.send(11)).has_value(), "first send() should succeed");
    ASCO_CHECK((co_await tx.send(12)).has_value(), "second send() should succeed");

    rx.stop();

    auto rejected = co_await tx.send(13);
    ASCO_CHECK(!rejected.has_value(), "send() should fail after receiver.stop()");
    ASCO_CHECK(rejected.error() == 13, "failed send() should return the rejected value");

    auto first = co_await rx.recv();
    ASCO_CHECK(
        first.has_value() && *first == 11, "after receiver.stop(), previously buffered values should still be receivable");

    auto second = co_await rx.recv();
    ASCO_CHECK(
        second.has_value() && *second == 12,
        "after receiver.stop(), the last buffered value should still be receivable before closure");

    auto closed = co_await rx.recv();
    ASCO_CHECK(!closed, "recv() should report closed only after buffered values are drained");

    ASCO_SUCCESS();
}

ASCO_TEST(channel_receiver_stop_wakes_blocked_sender_with_error) {
    constexpr std::size_t cap = sync::detail::channel_capacity;

    auto [tx, rx] = sync::channel<std::size_t>();

    for (std::size_t i = 0; i < cap; i++) {
        auto sent = co_await tx.send(i);
        ASCO_CHECK(sent.has_value(), "prefill send #{} should succeed", i);
    }

    std::atomic_bool blocked_completed{false};
    std::optional<std::size_t> rejected;

    auto blocked_sender = spawn([&]() -> future<void> {
        auto sent = co_await tx.send(cap);
        if (!sent) {
            rejected = sent.error();
        }
        blocked_completed.store(true, std::memory_order::release);
    });

    co_await yield_n(64);
    ASCO_CHECK(
        !blocked_completed.load(std::memory_order::acquire), "the extra sender should block while the channel is full");

    rx.stop();

    ASCO_CHECK(
        co_await wait([&]() { return blocked_completed.load(std::memory_order::acquire); }),
        "receiver.stop() should wake a sender blocked on backpressure");

    co_await blocked_sender;

    ASCO_CHECK(rejected.has_value() && *rejected == cap, "blocked sender should receive its rejected value after stop");

    auto first = co_await rx.recv();
    ASCO_CHECK(
        first.has_value() && *first == 0,
        "receiver.stop() should not discard values that were already buffered before the stop");

    ASCO_SUCCESS();
}

ASCO_TEST(channel_void_stop_drains_pending_signal_then_closes) {
    auto [tx, rx] = sync::channel<void>();

    ASCO_CHECK(co_await tx.send(), "send() should succeed for an open void channel");

    tx.stop();

    ASCO_CHECK(co_await rx.recv(), "recv() should still consume the buffered signal after sender.stop()");
    ASCO_CHECK(!(co_await rx.recv()), "recv() should report closed after the buffered signal is drained");
    ASCO_CHECK(!(co_await tx.send()), "send() should fail after sender.stop() on a void channel");

    ASCO_SUCCESS();
}