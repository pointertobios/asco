// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <semaphore>
#include <string>
#include <thread>

namespace asco::core {

class daemon {
public:
    daemon(std::string name);
    ~daemon();

    void awake();

private:
    struct init_waiter {
        daemon &self;

        ~init_waiter() { self.m_init_sem.acquire(); }
    };

protected:
    init_waiter start();

    void sleep_until_awake();
    void sleep_until_awake_for(const std::chrono::seconds &duration);
    void sleep_until_awake_for(const std::chrono::milliseconds &duration);
    void sleep_until_awake_for(const std::chrono::microseconds &duration);
    void sleep_until_awake_for(const std::chrono::nanoseconds &duration);
    void sleep_until_awake_before(const std::chrono::high_resolution_clock::time_point &time_point);

    virtual bool init();
    virtual bool run_once(std::stop_token &st);
    virtual void shutdown();

private:
    std::jthread m_dthread;
    std::string m_name{"asco::daemon"};
    ::pthread_t m_ptid{0};
    std::counting_semaphore<> m_init_sem{0};

    std::counting_semaphore<std::numeric_limits<int>::max()> m_sem{0};
};

};  // namespace asco::core
