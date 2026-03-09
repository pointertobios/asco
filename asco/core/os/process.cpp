// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "process.h"
#include <asco/core/os/process.h>

namespace asco::core::os {

cpu_set::cpu_set() noexcept
        : m_cpus{} {}

cpu_set cpu_set::with(this cpu_set &&self, std::size_t id) noexcept {
    self.m_cpus.push_back(id);
    return std::move(self);
}

std::vector<std::size_t> &cpu_set::get_all() noexcept { return m_cpus; }

thread_handle::thread_handle(std::thread::native_handle_type tid) noexcept
        : m_tid(tid) {}

thread_handle thread_handle::from(std::thread &t) noexcept { return {t.native_handle()}; }

thread_handle thread_handle::from(std::jthread &t) noexcept { return {t.native_handle()}; }

};  // namespace asco::core::os
