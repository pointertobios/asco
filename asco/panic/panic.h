// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace asco::panic {

[[noreturn]] void panic(std::string msg) noexcept;

[[noreturn]] void co_panic(std::string msg) noexcept;

};  // namespace asco::panic
