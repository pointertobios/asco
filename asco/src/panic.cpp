// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/panic.h"

#include <print>

namespace asco {

[[ASCO_NORETURN]] void panic(std::string_view msg, std::source_location sl) {
#ifdef ASCO_DEBUG_ENABLED
    throw panicked{msg, sl};
#endif
    std::string msg_final;
    if (msg.size()) {
        msg_final = std::format(": {}", msg);
    }
    std::println(stderr, "Panicked at {}:{}:{}{}", sl.file_name(), sl.line(), sl.column(), msg_final);
    std::abort();
}

};  // namespace asco
