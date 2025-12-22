// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <print>

#include <asco/future.h>
#include <asco/sync/channel.h>

using namespace asco;

future<int> async_main() {
    std::println("test_channel: start");

    // Test A: try_recv on empty channel returns non_object
    {
        auto [tx, rx] = channel<int>();
        auto r = rx.try_recv();
        if (r.has_value()) {
            std::println("test_channel: A FAILED - unexpected value on empty channel: {}", *r);
            co_return 1;
        }
        if (r.error() != pop_fail::non_object) {
            std::println("test_channel: A FAILED - expected non_object, got closed");
            co_return 1;
        }
        std::println("test_channel: A passed");
    }

    // Test B: send then try_recv returns the value; send returns nullopt on success
    {
        auto [tx, rx] = channel<int>();
        auto send_res = co_await tx.send(123);
        if (send_res.has_value()) {
            auto &[val, _] = *send_res;
            std::println("test_channel: B FAILED - send should succeed, got value back: {}", val);
            co_return 1;
        }

        auto r = rx.try_recv();
        if (!r.has_value() || *r != 123) {
            std::println("test_channel: B FAILED - expected 123, got {}", r.has_value() ? *r : -1);
            co_return 1;
        }

        // Now empty again
        auto r2 = rx.try_recv();
        if (r2.has_value() || r2.error() != pop_fail::non_object) {
            std::println("test_channel: B FAILED - expected empty after single recv");
            co_return 1;
        }
        std::println("test_channel: B passed");
    }

    // Test C: ordering with async recv()
    {
        auto [tx, rx] = channel<int>();
        constexpr int N = 5;
        for (int i = 0; i < N; ++i) {
            if (auto sr = co_await tx.send(i); sr.has_value()) {
                auto &[val, _] = *sr;
                std::println("test_channel: C FAILED - send returned value {}", val);
                co_return 1;
            }
        }

        for (int i = 0; i < N; ++i) {
            auto v = co_await rx.recv();
            if (!v.has_value() || *v != i) {
                std::println("test_channel: C FAILED - expected {}, got {}", i, v.has_value() ? *v : -1);
                co_return 1;
            }
        }

        // Should be empty now
        auto r = rx.try_recv();
        if (r.has_value() || r.error() != pop_fail::non_object) {
            std::println("test_channel: C FAILED - expected empty after draining");
            co_return 1;
        }
        std::println("test_channel: C passed");
    }

    // Test D: stop() behavior - further send fails; drain remaining then stopped flags
    {
        auto [tx, rx] = channel<int>();
        // enqueue a couple values
        (void)co_await tx.send(7);
        (void)co_await tx.send(8);
        tx.stop();

        // drain the two enqueued values
        auto v1 = co_await rx.recv();
        auto v2 = co_await rx.recv();
        if (!v1.has_value() || !v2.has_value() || *v1 != 7 || *v2 != 8) {
            std::println("test_channel: D FAILED - unexpected drained values");
            co_return 1;
        }

        // now channel should be considered stopped/drained
        if (!tx.is_stopped() || !rx.is_stopped()) {
            std::println("test_channel: D FAILED - expected stopped flags true");
            co_return 1;
        }

        // and no further items
        auto r = rx.try_recv();
        if (r.has_value() || r.error() != pop_fail::closed) {
            std::println(
                "test_channel: D FAILED - expected closed after stop and drain: {}", (size_t)(r.error()));
            co_return 1;
        }
        std::println("test_channel: D passed");
    }

    std::println("test_channel: all checks passed");
    co_return 0;
}
