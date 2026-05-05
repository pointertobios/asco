// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

namespace asco::core::mm {

struct cstring_pmr_tag {};

class cstring final {
public:
    cstring(std::string_view str);
    ~cstring();

    cstring(const cstring &) = delete;
    cstring &operator=(const cstring &) = delete;

    cstring(cstring &&) noexcept = delete;
    cstring &operator=(cstring &&) noexcept = delete;

    operator const char *() const noexcept { return m_data; }

private:
    char *m_data;
    union {
        std::size_t m_size;
        char m_sso[24];
    };
};

};  // namespace asco::core::mm
