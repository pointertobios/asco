// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <optional>

#include <asco/util/erased.h>

namespace asco::core::os {

class terminal {
public:
    static std::optional<terminal> get();
    ~terminal();

    terminal(terminal &&) = default;
    terminal &operator=(terminal &&) = default;

    std::size_t width() const;
    std::size_t height() const;

private:
    terminal();

    terminal with_extra(util::erased extra) && {
        m_extra = std::move(extra);
        return std::move(*this);
    }

    util::erased m_extra;
};

};  // namespace asco::core::os
