// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/wait_queue.h>

#include <asco/core/worker.h>

namespace asco::core {

yield wait_queue::wait() {
    auto _ = mut.lock();

    if (auto u = untriggered_notifications.load(morder::acquire)) {
        do {
            u = untriggered_notifications.load(morder::acquire);
            if (u == 0)
                goto do_suspend;
        } while (
            !untriggered_notifications.compare_exchange_weak(u, u - 1, morder::acq_rel, morder::acquire));
        return {};
    }

do_suspend:
    auto &w = worker::this_worker();
    auto this_task = w.current_task();
    w.suspend_task(this_task);
    waiters.push_back({&w, this_task});
    return {};
}

void wait_queue::notify(size_t n) {
    auto _ = mut.lock();

    while (n) {
        if (waiters.empty())
            break;
        auto [w, task_id] = waiters.front();
        waiters.pop_front();
        w->activate_task(task_id);
        n--;
    }
    if (n)
        untriggered_notifications.fetch_add(n, morder::release);
}

};  // namespace asco::core
