// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/assert.h>

namespace asco {

[[noreturn]] void assert_failed(std::string_view expr) { panic::co_panic("Assertion failed on {}", expr); }

[[noreturn]] void assert_failed(std::string_view expr, std::string_view hint) {
    panic::co_panic("Assertion failed on {}: {}", expr, hint);
}

};  // namespace asco
