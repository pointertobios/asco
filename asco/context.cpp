// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/context.h>

#include <asco/yield.h>

namespace asco::contexts {

std::shared_ptr<context> context::with_cancel() {
    return std::allocate_shared<context>(core::mm::allocator<context>(context::_allocator));
}

void context::cancel() noexcept {
    _cancelled.store(true, morder::release);
    _notify.notify_all();
}

bool context::is_cancelled() const noexcept { return _cancelled.load(morder::relaxed); }

yield<> context::operator co_await() noexcept {
    if (!is_cancelled())
        return _notify.wait();
    else
        return yield<>{};
}

};  // namespace asco::contexts
