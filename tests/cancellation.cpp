// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include <asco/cancellation.h>
#include <asco/core/runtime.h>
#include <asco/sync/semaphore.h>
#include <asco/test/test.h>
#include <asco/this_task.h>
#include <asco/yield.h>

using namespace asco;

namespace {

template<std::size_t N>
future<bool> acquire_for(sync::semaphore<N> &sem, std::chrono::steady_clock::duration max_wait) {
    const auto deadline = std::chrono::steady_clock::now() + max_wait;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sem.try_acquire()) {
            co_return true;
        }
        co_await this_task::yield();
    }
    co_return sem.try_acquire();
}

struct cancellable_never_ready {
    sync::binary_semaphore *started{};
    sync::binary_semaphore *callback_called{};

    std::unique_ptr<cancel_callback> cb;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<>) noexcept {
        cb = std::make_unique<cancel_callback>([this]() {
            if (callback_called)
                callback_called->release();
        });
        auto &w = core::worker::current();
        auto id = w.get_executor().current_execution();
        w.get_current_scheduler().suspend_current(id);

        if (started)
            started->release();
    }

    void await_resume() noexcept { cb.reset(); }
};

}  // namespace

ASCO_TEST(cancel_source_token_and_callback_basic) {
    core::cancel_source src;
    auto token = src.get_token();

    ASCO_CHECK(bool(token), "token should be valid");
    ASCO_CHECK(!token.cancel_requested(), "cancel should not be requested initially");

    src.request_cancel();
    ASCO_CHECK(
        token.cancel_requested(), "token.cancel_requested() should become true after request_cancel()");

    ASCO_SUCCESS();
}

ASCO_TEST(cancel_callback_destroyed_before_cancel_is_not_invoked) {
    sync::binary_semaphore callback_destroyed{0};
    sync::binary_semaphore callback_called{0};

    auto h = spawn([&]() -> future<void> {
        {
            cancel_callback cb{[&]() { callback_called.release(); }};
        }
        callback_destroyed.release();

        while (true) {
            co_await this_task::yield();
        }
    });

    ASCO_CHECK(
        co_await acquire_for(callback_destroyed, std::chrono::seconds{1}),
        "callback scope did not finish in time");

    h.cancel();

    ASCO_CHECK(
        !(co_await acquire_for(callback_called, std::chrono::milliseconds{200})),
        "destroyed callback should not be invoked after task cancellation");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");

    ASCO_SUCCESS();
}

ASCO_TEST(cancel_callbacks_execute_in_lifo_order_when_task_is_cancelled) {
    sync::binary_semaphore callbacks_registered{0};
    sync::semaphore<2> callbacks_called{0};
    std::vector<int> order;

    auto h = spawn([&]() -> future<void> {
        cancel_callback cb1{[&]() {
            order.push_back(1);
            callbacks_called.release();
        }};
        cancel_callback cb2{[&]() {
            order.push_back(2);
            callbacks_called.release();
        }};
        callbacks_registered.release();

        while (true) {
            co_await this_task::yield();
        }
    });

    ASCO_CHECK(
        co_await acquire_for(callbacks_registered, std::chrono::seconds{1}),
        "callbacks did not register in time");

    h.cancel();

    ASCO_CHECK(
        co_await acquire_for(callbacks_called, std::chrono::seconds{1}),
        "first cancel callback was not called in time");
    ASCO_CHECK(
        co_await acquire_for(callbacks_called, std::chrono::seconds{1}),
        "second cancel callback was not called in time");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");
    ASCO_CHECK(order.size() == 2, "callbacks should be invoked exactly twice");
    ASCO_CHECK(order[0] == 2 && order[1] == 1, "callbacks should be invoked in LIFO order");

    ASCO_SUCCESS();
}

ASCO_TEST(join_handle_cancel_triggers_callback_and_throws) {
    sync::binary_semaphore cb_registered{0};
    sync::binary_semaphore cancel_cb_called{0};

    auto h = spawn([&]() -> future<void> {
        cancel_callback cb{[&]() { cancel_cb_called.release(); }};
        cb_registered.release();

        while (true) {
            co_await this_task::yield();
        }
    });

    ASCO_CHECK(
        co_await acquire_for(cb_registered, std::chrono::seconds{1}), "callback did not register in time");

    h.cancel();

    ASCO_CHECK(
        co_await acquire_for(cancel_cb_called, std::chrono::seconds{1}),
        "cancel callback was not called in time");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");

    ASCO_SUCCESS();
}

ASCO_TEST(join_handle_cancel_works_when_task_is_suspended) {
    sync::binary_semaphore started{0};
    sync::binary_semaphore cancel_cb_called{0};

    auto h = spawn(
        [&]() -> future<void> { co_await cancellable_never_ready{&started, &cancel_cb_called, nullptr}; });

    ASCO_CHECK(co_await acquire_for(started, std::chrono::seconds{1}), "task did not start in time");

    h.cancel();

    ASCO_CHECK(
        co_await acquire_for(cancel_cb_called, std::chrono::seconds{1}),
        "cancel callback was not called in time");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");

    ASCO_SUCCESS();
}
