// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/mm/cstring.h>

#include <asco/core/mm/pool.h>

namespace asco::core::mm {

cstring::cstring(std::string_view str) {
    if (auto size = str.size() + 1; size > 24) {
        m_data = reinterpret_cast<char *>(pmr::get<cstring_pmr_tag>().allocate_bytes(size));
        m_size = str.size() + 1;
    } else {
        m_data = m_sso;
    }
    str.copy(m_data, str.size());
    m_data[str.size()] = '\0';
}

cstring::~cstring() {
    if (m_data == m_sso) {
        return;
    }

    pmr::get<cstring_pmr_tag>().deallocate_bytes(m_data, m_size);
}

};  // namespace asco::core::mm
