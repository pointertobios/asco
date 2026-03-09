// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <thread>
#include <vector>

namespace asco::core::os {

class cpu_set final {
public:
    cpu_set() noexcept;

    cpu_set(const cpu_set &) = delete;
    cpu_set &operator=(const cpu_set &) = delete;

    cpu_set(cpu_set &&) = default;
    cpu_set &operator=(cpu_set &&) = default;

    cpu_set with(this cpu_set &&self, std::size_t id) noexcept;

    std::vector<std::size_t> &get_all() noexcept;

private:
    std::vector<std::size_t> m_cpus;
};

class thread_handle final {
public:
    thread_handle(std::thread::native_handle_type tid) noexcept;

    static thread_handle from(std::thread &t) noexcept;
    static thread_handle from(std::jthread &t) noexcept;

    bool set_name(const std::string &name) noexcept;

    bool set_affinity(cpu_set cpuset) noexcept;

private:
    std::thread::native_handle_type m_tid;
};

};  // namespace asco::core::os
