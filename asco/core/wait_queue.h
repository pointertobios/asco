// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <list>

#include <asco/core/worker.h>
#include <asco/sync/spin.h>
#include <asco/utils/types.h>
#include <asco/yield.h>

namespace asco::core {

using namespace types;

class wait_queue {
public:
    wait_queue() = default;

    void notify(size_t n = 1);

private:
    spin<> mut;
    std::list<std::tuple<worker &, task_id>> waiters;
    atomic_size_t untriggered_notifications{0};

public:
    yield<std::optional<decltype(waiters)::iterator>> wait();
    void interrupt_wait(decltype(waiters)::iterator);
};

};  // namespace asco::core
