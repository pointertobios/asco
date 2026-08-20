// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <semaphore>
#include <string>
#include <thread>

namespace asco::core {

class daemon {
public:
    daemon();
    daemon(std::string name);
    virtual ~daemon();

    void run(std::stop_token &st);

    void start();

    void awake();

    void join();

protected:
    std::jthread m_thread;

    void wait_for_awake();

    virtual bool initialize() = 0;
    virtual bool run_once(std::stop_token &) = 0;
    virtual void finalize() = 0;

private:
    std::string m_name;

    bool m_joined{false};
    std::counting_semaphore<> m_idle_sem{0};
    std::binary_semaphore m_construct_sem{0};
};

};  // namespace asco::core
