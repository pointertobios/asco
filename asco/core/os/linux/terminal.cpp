// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/os/terminal.h>

#include <ncurses.h>
#include <unistd.h>

namespace asco::core::os {

std::optional<terminal> terminal::get() {
    if (!isatty(fileno(stdout))) {
        return std::nullopt;
    } else {
        return terminal{};
    }
}

terminal::~terminal() { endwin(); }

terminal::terminal() { initscr(); }

std::size_t terminal::width() const { return static_cast<std::size_t>(getmaxx(stdscr)); }

std::size_t terminal::height() const { return static_cast<std::size_t>(getmaxy(stdscr)); }

};  // namespace asco::core::os
