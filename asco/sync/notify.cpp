// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/yield.h"
#include <asco/sync/notify.h>

namespace asco::sync {

yield<> notify::wait() { return static_cast<yield<>>(core::wait_queue::wait()); }

void notify::notify_one() { core::wait_queue::notify(1, false); }

void notify::notify_all() { core::wait_queue::notify(std::numeric_limits<size_t>::max(), false); }

};  // namespace asco::sync
