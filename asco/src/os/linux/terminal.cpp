// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/os/terminal.h"

#include <optional>

#include <ncurses.h>
#include <unistd.h>

namespace asco::os {

std::optional<terminal> terminal::get() {
    if (!::isatty(::fileno(stdout))) {
        return std::nullopt;
    } else {
        return terminal{};
    }
}

terminal::~terminal() { ::endwin(); }

terminal::terminal() { ::initscr(); }

usize terminal::width() const { return static_cast<usize>(::getmaxx(stdscr)); }

usize terminal::height() const { return static_cast<usize>(::getmaxy(stdscr)); }

};  // namespace asco::os
