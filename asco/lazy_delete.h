// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UTILS_LAZY_DELETE_H
#define ASCO_UTILS_LAZY_DELETE_H 1

namespace asco::base {

template<typename T>
struct lazy_delete {
    T *value;

    inline ~lazy_delete() { delete value; }
};

template<typename T>
struct lazy_delete_array {
    T *value;

    inline ~lazy_delete_array() { delete[] value; }
};

};  // namespace asco::base

namespace asco {

using base::lazy_delete, base::lazy_delete_array;

};

#endif
