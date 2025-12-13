// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/core/wait_queue.h>

namespace asco::sync {

class notify : private core::wait_queue {
public:
    notify() = default;

    yield<> wait();
    void notify_one();
    void notify_all();

private:
};

};  // namespace asco::sync

namespace asco {

using sync::notify;

};  // namespace asco
