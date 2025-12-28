// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cassert>
#include <print>
#include <stdexcept>

#include <asco/future.h>
#include <asco/select.h>
#include <asco/time/sleep.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    std::println("test_select: start");

    // Test A: fastest branch wins (by completion time); index matches along_with order.
    {
        // NOTE: 这里传入 along_with(ctx) 的 ctx 是一个“可等待的分支”(waitable)，不是注入给其它分支的 select
        // 内部 ctx。
        auto ctx = context::with_cancel();
        std::atomic_bool cancel_observed{false};

        auto sele = asco::select{}        //
                        .along_with(ctx)  // index 0 (waitable)
                        .along_with([](std::shared_ptr<context>) -> future_spawn<float> {
                            co_await sleep_for(80ms);
                            co_return 3.14f;
                        })  // index 1
                        .along_with(
                            [](std::shared_ptr<context>, int x) -> future<int> {
                                co_await sleep_for(20ms);
                                co_return x;
                            },
                            42)  // index 2
                        .along_with([&](std::shared_ptr<context> c) -> future<void> {
                            co_await c;
                            cancel_observed.store(true, std::memory_order_release);
                        });  // index 3 (should not win)

        auto v = co_await sele;

        bool ok = false;
        std::visit(
            [&](auto &&res) {
                using T = std::decay_t<decltype(res)>;
                if constexpr (T::branch_index == 2) {
                    if (*res != 42) {
                        std::println("test_select: A FAILED - expected 42, got {}", *res);
                        return;
                    }
                    ok = true;
                } else {
                    std::println("test_select: A FAILED - expected branch 2 to win, got {}", T::branch_index);
                }
            },
            v);

        if (!ok) {
            co_return 1;
        }

        // 外部 ctx 不应被 select 取消。
        if (ctx->is_cancelled()) {
            std::println("test_select: A FAILED - external ctx should NOT be cancelled by select");
            co_return 1;
        }

        // Give the cancellation-waiting branch a moment to observe cancellation.
        co_await sleep_for(50ms);
        if (!cancel_observed.load(std::memory_order_acquire)) {
            std::println("test_select: A FAILED - expected cancel observer to run");
            co_return 1;
        }

        std::println("test_select: A passed");
    }

    // Test B: context(waitable) branch can win when it is cancelled first.
    {
        auto ctx = context::with_timeout(30ms);
        auto sele = asco::select{}        //
                        .along_with(ctx)  // index 0
                        .along_with([](std::shared_ptr<context>) -> future_spawn<int> {
                            co_await sleep_for(200ms);
                            co_return 1;
                        });  // index 1

        auto v = co_await sele;
        bool ok = false;
        std::visit(
            [&](auto &&res) {
                using T = std::decay_t<decltype(res)>;
                if constexpr (T::branch_index == 0) {
                    // void -> monostate
                    (void)*res;
                    ok = true;
                } else {
                    std::println("test_select: B FAILED - expected branch 0 to win, got {}", T::branch_index);
                }
            },
            v);

        if (!ok) {
            co_return 1;
        }
        std::println("test_select: B passed");
    }

    // Test C: void-returning async branch can win and returns monostate payload.
    {
        auto sele = asco::select{}                                                                //
                        .along_with([](std::shared_ptr<context>) -> future<void> { co_return; })  // index 0
                        .along_with([](std::shared_ptr<context>) -> future_spawn<int> {
                            co_await sleep_for(80ms);
                            co_return 9;
                        });  // index 1

        auto v = co_await sele;
        bool ok = false;
        std::visit(
            [&](auto &&res) {
                using T = std::decay_t<decltype(res)>;
                if constexpr (T::branch_index == 0) {
                    (void)*res;
                    ok = true;
                } else {
                    std::println("test_select: C FAILED - expected branch 0 to win, got {}", T::branch_index);
                }
            },
            v);

        if (!ok) {
            co_return 1;
        }
        std::println("test_select: C passed");
    }

    // Test D: exception propagates when the throwing branch wins.
    {
        auto sele = asco::select{}  //
                        .along_with([](std::shared_ptr<context>) -> future<int> {
                            co_await sleep_for(20ms);
                            throw std::runtime_error("select test error");
                        })
                        .along_with([](std::shared_ptr<context>) -> future_spawn<int> {
                            co_await sleep_for(200ms);
                            co_return 123;
                        });

        bool threw = false;
        try {
            (void)co_await sele;
        } catch (const std::runtime_error &) { threw = true; } catch (...) {
            std::println("test_select: D FAILED - unexpected exception type");
            co_return 1;
        }

        if (!threw) {
            std::println("test_select: D FAILED - expected runtime_error");
            co_return 1;
        }
        std::println("test_select: D passed");
    }

    std::println("test_select: all checks passed");
    co_return 0;
}
