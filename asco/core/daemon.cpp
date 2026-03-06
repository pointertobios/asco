// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/daemon.h>

#include <semaphore>
#include <stop_token>
#include <thread>
#ifdef __linux__
#    include <pthread.h>
#endif

#include <asco/core/os/process.h>

namespace asco::core {

daemon::daemon(std::string name)
        : m_name(std::move(name)) {}

void daemon::awake() { m_sem.release(); }

daemon::init_waiter daemon::start() {
    m_dthread = std::jthread{[this](std::stop_token st) {
#ifdef __linux__
        auto ptid = ::pthread_self();
        ::pthread_setname_np(ptid, m_name.c_str());
#else
        os::set_thread_name(m_dthread.native_handle(), m_name);
#endif

        if (!init()) {
            m_init_sem.release();
            shutdown();
            return;
        }
        m_init_sem.release();

        while (!st.stop_requested() && run_once(st))
            ;

        shutdown();
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

void daemon::sleep_until_awake_for(const std::chrono::seconds &duration) {
    (void)m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::milliseconds &duration) {
    (void)m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::microseconds &duration) {
    (void)m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::nanoseconds &duration) {
    (void)m_sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_before(const std::chrono::high_resolution_clock::time_point &time_point) {
    (void)m_sem.try_acquire_until(time_point);
}

bool daemon::init() { return true; }

bool daemon::run_once(std::stop_token &) {
    sleep_until_awake();
    return true;
}

void daemon::shutdown() {}

};  // namespace asco::core
