// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/core/daemon.h"

#include "asco/os/thread.h"

namespace asco::core {

daemon::daemon()
        : m_name{"asco::daemon"} {
    m_construct_sem.release();
}

daemon::daemon(std::string name)
        : m_name{name} {
    m_construct_sem.release();
}

daemon::~daemon() { join(); }

void daemon::start() {
    m_thread = std::jthread{[this](std::stop_token st) { run(st); }};
}

void daemon::awake() { m_idle_sem.release(); }

void daemon::join() {
    if (m_joined) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.request_stop();
        awake();
        m_thread.join();
        m_joined = true;
    }
}

void daemon::wait_for_awake() { m_idle_sem.acquire(); }

void daemon::run(std::stop_token &st) {
    m_construct_sem.acquire();
    
    os::thread_handle::this_thread().set_name(m_name);

    if (!initialize()) {
        return;
    }
    while (run_once(st)) {}
    finalize();
}

};  // namespace asco::core
