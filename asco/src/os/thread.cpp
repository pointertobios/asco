// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/os/thread.h"

namespace asco::os {

cpu_set::cpu_set()
        : m_cpus{} {}

cpu_set cpu_set::with(usize id) && {
    m_cpus.push_back(id);
    return std::move(*this);
}

std::vector<usize> &cpu_set::get_all() { return m_cpus; }

thread_handle::thread_handle(std::thread::native_handle_type tid)
        : m_tid{tid} {}

thread_handle thread_handle::from(std::thread &t) { return {t.native_handle()}; }

thread_handle thread_handle::from(std::jthread &t) { return {t.native_handle()}; }

};
