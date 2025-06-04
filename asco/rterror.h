// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_RTERROR_H
#define ASCO_RTERROR_H 1

#include <exception>
#include <stdexcept>
#include <string>

#include <asco/unwind/unwind.h>

namespace asco {

class runtime_error : public std::runtime_error {
public:
    runtime_error(const std::string &what)
            : std::runtime_error(
                  what
                  + std::format("\n\033[1mStack trace\033[0m:\n{:c}", unwind::function_unwinder::unwind())) {}
};

};  // namespace asco

#endif
