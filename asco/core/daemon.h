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

protected:
    void start();

    void sleep_until_awake_for();
    void sleep_until_awake_for(const std::chrono::seconds &duration);
    void sleep_until_awake_for(const std::chrono::milliseconds &duration);
    void sleep_until_awake_for(const std::chrono::microseconds &duration);
    void sleep_until_awake_for(const std::chrono::nanoseconds &duration);

    virtual bool init();
    virtual bool run_once();
    virtual void shutdown();

private:
    std::jthread dthread;
    std::string name{"asco::daemon"};
    ::pthread_t ptid{0};

    std::binary_semaphore sem{0};
};

};  // namespace asco::core
