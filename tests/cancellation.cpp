// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
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

struct cancellable_never_ready {
    sync::binary_semaphore *started{};
    sync::binary_semaphore *callback_called{};

    std::unique_ptr<cancel_callback> cb;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto &token = this_task::get_cancel_token();
        cb = std::make_unique<cancel_callback>(token, [this]() {
            if (callback_called)
                callback_called->release();
        });
        core::worker::current().suspend_current_handle(h);

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
    sync::binary_semaphore cb_registered{0};
    sync::binary_semaphore cancel_cb_called{0};

    auto h = spawn([&]() -> future<void> {
        auto &token = this_task::get_cancel_token();
        cancel_callback cb{token, [&]() { cancel_cb_called.release(); }};
        cb_registered.release();

        while (true) {
            co_await this_task::yield();
        }
    });

    co_await cb_registered.acquire();

    h.cancel();

    co_await cancel_cb_called.acquire();

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

    co_await started.acquire();

    h.cancel();

    co_await cancel_cb_called.acquire();

    bool threw_cancelled = false;
    try {
        co_await h;
    } catch (core::coroutine_cancelled &) { threw_cancelled = true; } catch (...) {
    }

    ASCO_CHECK(threw_cancelled, "awaiting a cancelled join_handle should throw coroutine_cancelled");

    ASCO_SUCCESS();
}
