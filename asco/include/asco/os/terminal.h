// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/types/erased.h"
#include "asco/types/int.h"

namespace asco::os {

class terminal {
public:
    static std::optional<terminal> get();
    ~terminal();

    terminal(terminal &&) = default;
    terminal &operator=(terminal &&) = default;

    usize width() const;
    usize height() const;

private:
    terminal();

    terminal with_extra(types::erased extra) && {
        m_extra = std::move(extra);
        return std::move(*this);
    }

    types::erased m_extra;
};

};  // namespace asco::os
