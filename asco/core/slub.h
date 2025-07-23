// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SLUB_H
#define ASCO_SLUB_H 1

#include <asco/utils/pubusing.h>

namespace asco::core::slub {

using namespace types;

template<typename T>
struct object {
    object<T> *next;
    size_t len;

    static object<T> *from(T *ptr) { return reinterpret_cast<object<T> *>(ptr); }

    T *obj() noexcept { return reinterpret_cast<T *>(this); }
};

};  // namespace asco::core::slub

#endif
