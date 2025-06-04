// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_EXCEPTION_H
#define ASCO_EXCEPTION_H 1

#include <exception>
#include <format>
#include <stdexcept>

#include <asco/future.h>
#include <asco/rterror.h>
#include <asco/unwind/coro_unwind.h>

namespace asco {

class exception : public std::runtime_error {
public:
    template<typename R = RT>
        requires core::is_runtime<R>
    exception(const std::string &what)
            : std::runtime_error(
                  what + ({
                      if (!RT::__worker::in_worker())
                          throw asco::runtime_error(
                              "[ASCO] asco::exception must be constructed in asco runtime.");
                      "";
                  })
                  + std::format("\n\033[1mStack trace\033[0m:\n{:c}", unwind::coro_unwinder::unwind())) {}
};

class inner_exception : public exception {
public:
    inner_exception(const std::string &what)
            : exception(what) {}
};

};  // namespace asco

#endif
