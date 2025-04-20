// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SUSPEND_H
#define ASCO_SUSPEND_H 1

#include <cstddef>

#include <asco/future.h>

namespace asco {

template<typename R = RT>
struct suspend {
    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        auto id = RT::__worker::get_worker()->current_task_id();
        auto worker = RT::__worker::get_worker_from_task_id(id);
        if (worker->sc.task_exists(id))
            worker->sc.suspend(id);
        return true;
    }

    void await_resume() {}
};

};

#endif

