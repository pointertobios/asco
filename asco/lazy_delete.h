// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_LAZY_DELETE_H
#define ASCO_UTILS_LAZY_DELETE_H 1

#include <concepts>

namespace asco::base {

template<typename T>
struct lazy_delete {
    T *value;

    template<std::invocable F>
        requires std::is_same_v<T *, std::invoke_result_t<F>>
    lazy_delete(F f)
            : value(f()) {}

    inline ~lazy_delete() { delete value; }
};

};  // namespace asco::base

namespace asco {

using base::lazy_delete;

};

#endif
