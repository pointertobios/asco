// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <optional>

namespace asco::core::os {

class terminal {
public:
    static std::optional<terminal> get();
    ~terminal();

    std::size_t width() const;
    std::size_t height() const;

    void clear_line();

private:
    terminal();
};

};  // namespace asco::core::os
