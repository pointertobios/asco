// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include <asco/cancellation.h>
#include <asco/core/runtime.h>
#include <asco/test/test.h>
#include <asco/this_task.h>
#include <asco/yield.h>

using namespace asco;

namespace {

template<typename Pred>
future<bool> wait_until(Pred &&pred, std::size_t max_spins = 4096) {
    for (std::size_t i = 0; i < max_spins; i++) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await this_task::yield();
    }
    co_return false;
}

struct cancellable_never_ready {
    std::atomic_bool *started{};
    std::atomic_bool *callback_called{};

    std::unique_ptr<cancel_callback> cb;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto &token = this_task::get_cancel_token();
        cb = std::make_unique<cancel_callback>(token, [this]() {
            if (callback_called) {
                callback_called->store(true, std::memory_order::release);
            }
        });

        if (started) {
            started->store(true, std::memory_order::release);
        }

        core::worker::current().suspend_current_handle(h);
    }

    void await_resume() noexcept { cb.reset(); }
};

}  // namespace

ASCO_TEST(cancel_source_token_and_callback_basic) {
    core::cancel_source src;
    auto token = src.get_token();

    ASCO_CHECK(bool(token), "token should be valid");
    ASCO_CHECK(!token.cancel_requested(), "cancel should not be requested initially");

    {
        std::atomic_int called{0};
        cancel_callback cb{token, [&]() { called.fetch_add(1, std::memory_order::acq_rel); }};

        src.request_cancel();
        ASCO_CHECK(
            token.cancel_requested(), "token.cancel_requested() should become true after request_cancel()");

        src.invoke_callbacks();
        ASCO_CHECK(called.load(std::memory_order::acquire) == 1, "callback should be invoked exactly once");
    }

    {
        std::atomic_int called{0};
        {
            cancel_callback cb{token, [&]() { called.fetch_add(1, std::memory_order::acq_rel); }};
        }

        src.invoke_callbacks();
        ASCO_CHECK(called.load(std::memory_order::acquire) == 0, "destroyed callback should not be invoked");
    }

    {
        std::vector<int> order;
        cancel_callback cb1{token, [&]() { order.push_back(1); }};
        cancel_callback cb2{token, [&]() { order.push_back(2); }};

        src.invoke_callbacks();

        ASCO_CHECK(order.size() == 2, "callbacks should be invoked");
        ASCO_CHECK(order[0] == 2 && order[1] == 1, "callbacks should be invoked in LIFO order");
    }

    ASCO_SUCCESS();
}

ASCO_TEST(join_handle_cancel_triggers_callback_and_throws) {
    std::atomic_bool cb_registered{false};
    std::atomic_bool cancel_cb_called{false};

    auto h = spawn([&]() -> future<void> {
        auto &token = this_task::get_cancel_token();
        cancel_callback cb{token, [&]() { cancel_cb_called.store(true, std::memory_order::release); }};
        cb_registered.store(true, std::memory_order::release);

        while (true) {
            co_await this_task::yield();
        }
    });

    ASCO_CHECK(
        co_await wait_until([&]() { return cb_registered.load(std::memory_order::acquire); }),
        "task should register cancel callback before cancellation");

    co_await h.cancel();

    ASCO_CHECK(
        co_await wait_until([&]() { return cancel_cb_called.load(std::memory_order::acquire); }),
        "cancel callback should eventually be invoked after join_handle::cancel()");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");

    ASCO_SUCCESS();
}

ASCO_TEST(join_handle_cancel_works_when_task_is_suspended) {
    std::atomic_bool started{false};
    std::atomic_bool cancel_cb_called{false};

    auto h = spawn(
        [&]() -> future<void> { co_await cancellable_never_ready{&started, &cancel_cb_called, nullptr}; });

    ASCO_CHECK(
        co_await wait_until([&]() { return started.load(std::memory_order::acquire); }),
        "task should reach suspension point before cancellation");

    co_await h.cancel();

    ASCO_CHECK(
        co_await wait_until([&]() { return cancel_cb_called.load(std::memory_order::acquire); }),
        "cancel callback should eventually be invoked even if the task is suspended");

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");

    ASCO_SUCCESS();
}
