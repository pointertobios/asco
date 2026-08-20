// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <new>

#include "asco/types/int.h"

namespace asco {

inline constexpr usize cacheline_size = std::hardware_destructive_interference_size;

using morder = std::memory_order;

};  // namespace asco
