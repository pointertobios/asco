// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <semaphore>
#include <stop_token>
#ifdef __linux__
#    include <pthread.h>
#endif

#include <asco/core/daemon.h>

namespace asco::core {

daemon::daemon(std::string name)
        : m_name(std::move(name)) {}

void daemon::awake() { m_sem.release(); }

daemon::init_waiter daemon::start() {
    m_dthread = std::jthread{[&self = *this](std::stop_token st) {
#ifdef __linux__
        self.m_ptid = ::pthread_self();
        ::pthread_setname_np(self.m_ptid, self.m_name.c_str());
#endif

        if (!self.init()) {
            self.m_init_sem.release();
            self.shutdown();
            return;
        }
        self.m_init_sem.release();

        while (!st.stop_requested() && self.run_once(st))
            ;

        self.shutdown();
    }};

    return {*this};
}

daemon::~daemon() {
    if (m_dthread.joinable()) {
        m_dthread.request_stop();
        awake();
        m_dthread.join();
    }
}

void daemon::sleep_until_awake() { m_sem.acquire(); }

void daemon::sleep_until_awake_for(const std::chrono::seconds &duration) { m_sem.try_acquire_for(duration); }

void daemon::sleep_until_awake_for(const std::chrono::milliseconds &duration) {
    m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::microseconds &duration) {
    m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::nanoseconds &duration) {
    m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_before(const std::chrono::high_resolution_clock::time_point &time_point) {
    m_sem.try_acquire_until(time_point);
}

bool daemon::init() { return true; }

bool daemon::run_once(std::stop_token &) {
    sleep_until_awake();
    return true;
}

void daemon::shutdown() {}

};  // namespace asco::core
