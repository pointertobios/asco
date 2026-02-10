// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <new>

namespace asco::util {

constexpr std::size_t cacheline =
    std::hardware_destructive_interference_size ? std::hardware_destructive_interference_size : 16;

};
