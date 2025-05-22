// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_EXCEPTION_H
#define ASCO_EXCEPTION_H 1

#include <exception>
#include <format>
#include <stdexcept>

#include <asco/future.h>
#include <asco/unwind/coro_unwind.h>

namespace asco {

class exception : public std::runtime_error {
public:
    exception(const std::string &what)
            : std::runtime_error(
                  what + std::format("\n\033[1mStack trace\033[0m:\n{:c}", unwind::coro_unwinder::unwind())) {
    }
};

};  // namespace asco

#endif
