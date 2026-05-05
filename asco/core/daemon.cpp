// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/daemon.h>

#include <chrono>
#include <semaphore>
#include <string>
#include <stop_token>
#include <thread>
#include <utility>

#include <asco/core/os/process.h>

namespace asco::core {

daemon::daemon(std::string name)
        : m_name(std::move(name)) {}

void daemon::awake() {
    m_sem.release();
    for (auto &hook : m_awake_hooks) {
        hook();
    }
}

daemon::init_waiter daemon::start() {
    m_dthread = std::jthread{[this](std::stop_token st) {
        m_b.arrive_and_wait();

        os::thread_handle::from(m_dthread).set_name(m_name);

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

    m_b.arrive_and_wait();

    return {*this};
}

daemon::~daemon()
#ifdef ASCO_TESTING
    noexcept(false)
#endif
{
    join();
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

void daemon::sleep_until_awake_before(const std::chrono::steady_clock::time_point &time_point) {
    (void)m_sem.try_acquire_until(time_point);
}

bool daemon::init() { return true; }

bool daemon::run_once(std::stop_token &) {
    sleep_until_awake();
    return true;
}

void daemon::shutdown() {}

void daemon::register_awake_hook(std::function<void()> hook) { m_awake_hooks.push_back(std::move(hook)); }

void daemon::join() {
    if (joined) {
        return;
    }

    if (m_dthread.joinable()) {
        m_dthread.request_stop();
        awake();
        m_dthread.join();
        joined = true;
    }
}

};  // namespace asco::core
