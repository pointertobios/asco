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
        : name(std::move(name)) {}

void daemon::awake() { sem.release(); }

daemon::init_waiter daemon::start() {
    dthread = std::jthread{[&self = *this](std::stop_token st) {
#ifdef __linux__
        self.ptid = ::pthread_self();
        ::pthread_setname_np(self.ptid, self.name.c_str());
#endif

        if (!self.init()) {
            self.init_sem.release();
            self.shutdown();
            return;
        }
        self.init_sem.release();

        while (!st.stop_requested() && self.run_once(st));

        self.shutdown();
    }};

    return {*this};
}

daemon::~daemon() {
    if (dthread.joinable()) {
        dthread.request_stop();
        awake();
        dthread.join();
    }
}

void daemon::sleep_until_awake() { sem.acquire(); }

void daemon::sleep_until_awake_for(const std::chrono::seconds &duration) { sem.try_acquire_for(duration); }

void daemon::sleep_until_awake_for(const std::chrono::milliseconds &duration) {
    sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::microseconds &duration) {
    sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_for(const std::chrono::nanoseconds &duration) {
    sem.try_acquire_for(duration);
}

void daemon::sleep_until_awake_before(const std::chrono::high_resolution_clock::time_point &time_point) {
    sem.try_acquire_until(time_point);
}

bool daemon::init() { return true; }

bool daemon::run_once(std::stop_token &) {
    sleep_until_awake();
    return true;
}

void daemon::shutdown() {}

};  // namespace asco::core
