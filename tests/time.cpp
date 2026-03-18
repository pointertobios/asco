// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>

#include <asco/core/cancellation.h>
#include <asco/test/test.h>
#include <asco/this_task.h>
#include <asco/time/interval.h>
#include <asco/time/sleep.h>

using namespace asco;

namespace {

future<void> yield_n(std::size_t n) {
    for (std::size_t i = 0; i < n; i++) {
        co_await this_task::yield();
    }
}

template<typename Pred>
future<bool> yield_wait(Pred &&pred, std::size_t max_yields) {
    for (std::size_t i = 0; i < max_yields; i++) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await this_task::yield();
    }
    co_return std::invoke(pred);
}

template<typename Pred>
future<bool> yield_wait_for(Pred &&pred, std::chrono::steady_clock::duration max_wait) {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + max_wait;
    while (steady_clock::now() < deadline) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await this_task::yield();
    }
    co_return std::invoke(pred);
}

}  // namespace

ASCO_TEST(sleep_until_past_returns_immediately) {
    using namespace std::chrono;

    // 过去的时间点：timer::register_timer 应返回无效 timer_id，sleep_until 直接返回。
    co_await time::sleep_until(steady_clock::now() - 1ms);

    ASCO_SUCCESS();
}

ASCO_TEST(sleep_for_negative_returns_immediately) {
    using namespace std::chrono;

    // 负 duration 等价于 sleep_until(past)，应立即返回。
    co_await time::sleep_for(-1ms);

    ASCO_SUCCESS();
}

ASCO_TEST(sleep_for_waits_at_least_duration) {
    using namespace std::chrono;

    constexpr auto duration = 30ms;
    constexpr auto eps = 1ms;

    std::atomic<long long> start_ticks{0};
    std::atomic<long long> end_ticks{0};

    auto h = spawn([&]() -> future<void> {
        start_ticks.store(steady_clock::now().time_since_epoch().count(), std::memory_order::release);
        co_await time::sleep_for(duration);
        end_ticks.store(steady_clock::now().time_since_epoch().count(), std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); }, co_await yield_wait_for([&]() { return h.await_ready(); }, 2s),
        "sleep_for did not complete within bounded yields (timer may be broken)");

    // 此时 await_ready() 为 true，co_await 不会挂起。
    co_await h;

    auto start = steady_clock::duration{start_ticks.load(std::memory_order::acquire)};
    auto end = steady_clock::duration{end_ticks.load(std::memory_order::acquire)};
    auto elapsed = end - start;

    auto elapsed_us = duration_cast<microseconds>(elapsed).count();
    auto want_us = duration_cast<microseconds>(duration - eps).count();

    ASCO_CHECK(
        elapsed >= duration - eps, "sleep_for returned too early: elapsed={}us, want>={}us", elapsed_us,
        want_us);

    ASCO_SUCCESS();
}

ASCO_TEST(sleep_for_can_be_cancelled) {
    using namespace std::chrono;

    std::atomic_bool entered{false};
    std::atomic_bool finished{false};

    auto h = spawn([&]() -> future<void> {
        entered.store(true, std::memory_order::release);
        co_await time::sleep_for(5s);
        finished.store(true, std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); },
        co_await yield_wait([&]() { return entered.load(std::memory_order::acquire); }, 1024),
        "sleep task did not start in time");

    // 给协程机会进入 sleep 并完成 timer 注册/挂起。
    co_await yield_n(64);

    h.cancel();

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); }, co_await yield_wait([&]() { return h.await_ready(); }, 4096),
        "cancelled join_handle did not become ready in time");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled sleep task should throw coroutine_cancelled");
    ASCO_CHECK(!finished.load(std::memory_order::acquire), "cancelled task should not reach finished state");

    ASCO_SUCCESS();
}

ASCO_TEST(sleep_ordering_short_finishes_before_long) {
    using namespace std::chrono;

    std::atomic_bool short_done{false};
    std::atomic_bool long_done{false};

    std::atomic<long long> short_end_ticks{0};
    std::atomic<long long> long_end_ticks{0};

    auto short_h = spawn([&]() -> future<void> {
        co_await time::sleep_for(1us);
        short_end_ticks.store(steady_clock::now().time_since_epoch().count(), std::memory_order::release);
        short_done.store(true, std::memory_order::release);
    });

    auto long_h = spawn([&]() -> future<void> {
        co_await time::sleep_for(800ms);
        long_end_ticks.store(steady_clock::now().time_since_epoch().count(), std::memory_order::release);
        long_done.store(true, std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() {
            short_h.cancel();
            long_h.cancel();
        },
        co_await yield_wait_for([&]() { return short_done.load(std::memory_order::acquire); }, 1s),
        "short sleep did not finish in time");

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() {
            short_h.cancel();
            long_h.cancel();
        },
        co_await yield_wait_for([&]() { return long_done.load(std::memory_order::acquire); }, 3s),
        "long sleep did not finish in time");

    const auto short_end = steady_clock::duration{short_end_ticks.load(std::memory_order::acquire)};
    const auto long_end = steady_clock::duration{long_end_ticks.load(std::memory_order::acquire)};
    ASCO_CHECK(short_end <= long_end, "ordering violated: short finished after long");

    co_await short_h;
    co_await long_h;

    ASCO_SUCCESS();
}

ASCO_TEST(interval_first_tick_is_based_on_construction_time) {
    using namespace std::chrono;

    // 语义（按当前实现）：
    // interval 的基准是构造时刻 begin_time，第一次 tick 的目标时间点为 (begin_time + 1 * duration)。
    // 因此“紧接着调用 tick”应当等待接近一个周期。
    //
    // 注意：interval 必须在被测协程内构造，否则 spawn 的调度间隙会把等待时间吃掉，导致误判。
    constexpr auto duration = 80ms;
    constexpr auto eps = 10ms;

    std::atomic<long long> elapsed_us{0};

    auto h = spawn([&]() -> future<void> {
        time::interval it{duration};
        const auto start = steady_clock::now();
        co_await it.tick();
        const auto end = steady_clock::now();
        elapsed_us.store(duration_cast<microseconds>(end - start).count(), std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); }, co_await yield_wait_for([&]() { return h.await_ready(); }, 2s),
        "first tick did not complete in time (timer may be broken)");

    co_await h;

    const auto got_us = elapsed_us.load(std::memory_order::acquire);
    const auto want_us = duration_cast<microseconds>(duration - eps).count();
    ASCO_CHECK(got_us >= want_us, "first tick returned too early: elapsed={}us, want>={}us", got_us, want_us);

    ASCO_SUCCESS();
}

ASCO_TEST(interval_second_tick_waits_at_least_duration) {
    using namespace std::chrono;

    // 这个测试需要同时满足：
    // 1) 第二次 tick 不会“过早返回”；
    // 2) 即使底层 timer 出现问题，也不会让整个测试永远挂起。
    // 因此使用 spawn + 有界等待（超时即取消）来避免偶发的永不完成。

    // 语义（按当前实现）：
    // 第 N 次 tick 的目标时间点为 (begin_time + N * duration)。
    // 因此在第一次 tick 结束后立刻调用第二次 tick，应当再次等待接近一个周期。
    constexpr auto duration = 80ms;
    constexpr auto eps = 10ms;

    std::atomic<long long> elapsed_us{0};

    auto h = spawn([&]() -> future<void> {
        time::interval it{duration};
        co_await it.tick();

        const auto start = steady_clock::now();
        co_await it.tick();
        const auto end = steady_clock::now();

        elapsed_us.store(duration_cast<microseconds>(end - start).count(), std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); }, co_await yield_wait_for([&]() { return h.await_ready(); }, 3s),
        "second tick did not complete in time (timer may be broken)");

    co_await h;

    const auto got_us = elapsed_us.load(std::memory_order::acquire);
    const auto want_us = duration_cast<microseconds>(duration - eps).count();
    ASCO_CHECK(
        got_us >= want_us, "second tick returned too early: elapsed={}us, want>={}us", got_us, want_us);

    ASCO_SUCCESS();
}

ASCO_TEST(interval_overrun_tick_does_not_wait) {
    using namespace std::chrono;

    // 语义（按当前实现）：
    // 若调用 tick 时已经超过目标时间点 (begin_time + N * duration)，tick 应立即返回。
    constexpr auto duration = 50ms;
    constexpr auto max_instant = 25ms;

    std::atomic<long long> elapsed_us{0};

    auto h = spawn([&]() -> future<void> {
        time::interval it{duration};
        co_await it.tick();

        co_await time::sleep_for(180ms);

        const auto start = steady_clock::now();
        co_await it.tick();
        const auto end = steady_clock::now();

        elapsed_us.store(duration_cast<microseconds>(end - start).count(), std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); }, co_await yield_wait_for([&]() { return h.await_ready(); }, 3s),
        "overrun tick did not complete in time (timer may be broken)");

    co_await h;

    const auto got_us = elapsed_us.load(std::memory_order::acquire);
    const auto want_us = duration_cast<microseconds>(max_instant).count();
    ASCO_CHECK(got_us < want_us, "overrun tick unexpectedly waited: elapsed={}us", got_us);
    ASCO_SUCCESS();
}

ASCO_TEST(interval_tick_can_be_cancelled) {
    using namespace std::chrono;

    std::atomic_bool entered{false};
    std::atomic_bool sleeping{false};
    std::atomic_bool finished{false};

    auto h = spawn([&]() -> future<void> {
        // 让第二次 tick 必然进入 sleep 状态，然后取消。
        // 按当前实现：第 N 次 tick 等待到 begin_time + N * duration。
        // 因此第一次 tick 结束后立刻调用第二次 tick，必然会等待一个周期。
        time::interval it{100ms};
        entered.store(true, std::memory_order::release);
        co_await it.tick();
        sleeping.store(true, std::memory_order::release);
        co_await it.tick();
        finished.store(true, std::memory_order::release);
    });

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); },
        co_await yield_wait_for([&]() { return entered.load(std::memory_order::acquire); }, 2s),
        "interval task did not start in time");

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); },
        co_await yield_wait_for([&]() { return sleeping.load(std::memory_order::acquire); }, 2s),
        "interval task did not reach second tick in time");

    // 给协程机会进入 sleep 并完成 timer 注册/挂起。
    co_await yield_n(64);

    h.cancel();

    ASCO_CHECK_WITH_FAILCALLBACK(
        [&]() { h.cancel(); }, co_await yield_wait_for([&]() { return h.await_ready(); }, 2s),
        "cancelled interval task did not become ready in time");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled interval task should throw coroutine_cancelled");
    ASCO_CHECK(
        !finished.load(std::memory_order::acquire),
        "cancelled interval task should not reach finished state");

    ASCO_SUCCESS();
}
