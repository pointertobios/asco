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

bool set_thread_affinity(std::thread::native_handle_type tid, cpu_set cpuset) noexcept;

bool set_thread_name(std::thread::native_handle_type tid, const std::string &name) noexcept;

};  // namespace asco::core::os
