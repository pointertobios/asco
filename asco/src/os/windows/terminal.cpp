// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/os/terminal.h"

#include <optional>

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "asco/panic.h"

namespace asco::os {

std::optional<terminal> terminal::get() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    BOOL ok = GetConsoleMode(h, &mode);
    if (!h || h == INVALID_HANDLE_VALUE || !ok) {
        return std::nullopt;
    } else {
        return terminal{}.with_extra({HANDLE{h}});
    }
}

terminal::terminal() = default;

terminal::~terminal() = default;

usize terminal::width() const {
    HANDLE h = m_extra.get<HANDLE>();
    CONSOLE_SCREEN_BUFFER_INFO info{};
    BOOL ok = GetConsoleScreenBufferInfo(h, &info);
    if (!ok) {
        panic("jungle::os::terminal: 获取终端宽度失败");
    }
    return info.srWindow.Right - info.srWindow.Left + 1;
}

usize terminal::height() const {
    HANDLE h = m_extra.get<HANDLE>();
    CONSOLE_SCREEN_BUFFER_INFO info{};
    BOOL ok = GetConsoleScreenBufferInfo(h, &info);
    if (!ok) {
        panic("jungle::os::terminal: 获取终端高度失败");
    }
    return info.srWindow.Bottom - info.srWindow.Top + 1;
}

};  // namespace asco::os
