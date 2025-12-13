// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <print>

#include <asco/context.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/time/sleep.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    std::println("test_context: start");

    // Test A: manual cancel wakes waiters and updates state
    {
        auto ctx = context::with_cancel();
        if (ctx->is_cancelled()) {
            std::println("test_context: A FAILED - context should start non-cancelled");
            co_return 1;
        }

        std::atomic<bool> resumed{false};

        auto waiter = co_invoke([ctx, &resumed]() -> future_spawn<void> {
            co_await ctx;
            resumed.store(true, std::memory_order_release);
            co_return;
        });

        co_await sleep_for(5ms);
        if (resumed.load(std::memory_order_acquire)) {
            std::println("test_context: A FAILED - waiter resumed before cancel");
            co_return 1;
        }

        ctx->cancel();
        co_await waiter;

        if (!ctx->is_cancelled()) {
            std::println("test_context: A FAILED - context did not transition to cancelled");
            co_return 1;
        }
        if (!resumed.load(std::memory_order_acquire)) {
            std::println("test_context: A FAILED - waiter did not observe cancellation");
            co_return 1;
        }
        std::println("test_context: A passed");
    }

    // Test B: timeout-based cancellation occurs after delay
    {
        constexpr auto timeout = 40ms;
        auto ctx = context::with_timeout(timeout);
        if (ctx->is_cancelled()) {
            std::println("test_context: B FAILED - timeout context cancelled immediately");
            co_return 1;
        }

        std::atomic<bool> resumed{false};

        auto waiter = co_invoke([ctx, &resumed]() -> future_spawn<void> {
            co_await ctx;
            resumed.store(true, std::memory_order_release);
            co_return;
        });

        co_await sleep_for(timeout / 4);
        if (ctx->is_cancelled()) {
            std::println("test_context: B FAILED - timeout fired prematurely");
            co_return 1;
        }

        co_await waiter;
        if (!resumed.load(std::memory_order_acquire)) {
            std::println("test_context: B FAILED - waiter did not resume after timeout");
            co_return 1;
        }
        if (!ctx->is_cancelled()) {
            std::println("test_context: B FAILED - context not cancelled after timeout");
            co_return 1;
        }
        std::println("test_context: B passed");
    }

    // Test C: wait after cancellation resumes immediately
    {
        auto ctx = context::with_cancel();
        ctx->cancel();
        std::atomic<bool> resumed{false};
        auto waiter = co_invoke([ctx, &resumed]() -> future_spawn<void> {
            co_await ctx;
            resumed.store(true, std::memory_order_release);
            co_return;
        });
        co_await waiter;
        if (!resumed.load(std::memory_order_acquire)) {
            std::println("test_context: C FAILED - waiter did not resume immediately after cancellation");
            co_return 1;
        }
        std::println("test_context: C passed");
    }

    std::println("test_context: all checks passed");
    co_return 0;
}
