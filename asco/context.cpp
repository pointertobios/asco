// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/context.h>

namespace asco::contexts {

std::shared_ptr<context> context::with_cancel() {
    return std::allocate_shared<context>(core::mm::allocator<context>(context::_allocator));
}

future<void> context::cancel() {
    _cancelled.store(true, morder::release);
    _notify.notify_all();
    if (auto cb = co_await _cancel_callback.read(); *cb) {
        (*cb)();
    }
}

bool context::is_cancelled() const noexcept { return _cancelled.load(morder::relaxed); }

yield<> context::operator co_await() noexcept {
    if (!is_cancelled())
        return _notify.wait();
    else
        return yield<>{};
}

yield<> operator co_await(const std::shared_ptr<context> &ctx) noexcept { return ctx->operator co_await(); }

future<void> context::set_cancel_callback(std::function<void()> &&callback) {
    *(co_await _cancel_callback.write()) = std::move(callback);
}

};  // namespace asco::contexts
