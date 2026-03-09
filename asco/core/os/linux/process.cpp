// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/os/process.h>

#include <pthread.h>
#include <sched.h>

namespace asco::core::os {

bool set_thread_affinity(std::thread::native_handle_type tid, cpu_set cpuset) noexcept {
    ::cpu_set_t cs;
    CPU_ZERO(&cs);
    for (auto id : cpuset.get_all()) {
        CPU_SET(id, &cs);
    }
    return ::pthread_setaffinity_np(tid, sizeof(cs), &cs) == 0;
}

bool set_thread_name(std::thread::native_handle_type tid, const std::string &name) noexcept {
    return ::pthread_setname_np(tid, name.c_str()) == 0;
}

bool thread_handle::set_name(const std::string &name) noexcept { return set_thread_name(m_tid, name); }

bool thread_handle::set_affinity(cpu_set cpuset) noexcept {
    return set_thread_affinity(m_tid, std::move(cpuset));
}

};  // namespace asco::core::os
