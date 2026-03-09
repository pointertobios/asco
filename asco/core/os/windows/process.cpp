// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/os/process.h>

#include <windows.h>

namespace asco::core::os {

bool set_thread_affinity(std::thread::native_handle_type tid, cpu_set cpuset) noexcept {
    const auto &ids = cpuset.get_all();
    if (ids.empty()) {
        return false;
    }

    const WORD group_cnt = GetActiveProcessorGroupCount();
    DWORD base_index = 0;

    bool found = false;
    GROUP_AFFINITY ga{};

    for (WORD g = 0; g < group_cnt; g++) {
        const DWORD group_cpu_cnt = GetActiveProcessorCount(g);
        KAFFINITY mask = 0;

        for (auto id : ids) {
            if (id >= base_index && id < base_index + group_cpu_cnt) {
                mask |= (KAFFINITY(1) << (id - base_index));
            }
        }

        if (mask != 0) {
            if (found) {
                return false;
            }
            found = true;
            ga.Group = g;
            ga.Mask = mask;
        }

        base_index += group_cpu_cnt;
    }

    if (!found) {
        return false;
    }

    return SetThreadGroupAffinity(tid, &ga, nullptr) != 0;
}

bool set_thread_name(std::thread::native_handle_type tid, const std::string &name) noexcept {
    const auto wname = std::wstring(name.begin(), name.end());
    const auto hr = SetThreadDescription(tid, wname.c_str());
    return SUCCEEDED(hr);
}

bool thread_handle::set_name(const std::string &name) noexcept { return set_thread_name(m_tid, name); }

bool thread_handle::set_affinity(cpu_set cpuset) noexcept {
    return set_thread_affinity(m_tid, std::move(cpuset));
}

};  // namespace asco::core::os
