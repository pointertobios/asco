// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_DYNVAR_H
#define ASCO_UTILS_DYNVAR_H 1

#include <functional>

#include <asco/utils/pubusing.h>

namespace asco::inner {

struct dynvar {
    using destructor = std::function<void(void *)>;

    size_t type;
    void *p;
    destructor deconstruct;
};

};  // namespace asco::inner

#endif
