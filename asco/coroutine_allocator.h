// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_COROUTINE_ALLOCATOR_H
#define ASCO_COROUTINE_ALLOCATOR_H 1

#include <asco/utils/pubusing.h>

namespace asco::base {

using namespace types;

struct coroutine_allocator {
    constexpr inline static size_t header_size = 2;

    static inline void *allocate(std::size_t n) noexcept {
        auto *p = static_cast<size_t *>(::operator new(n + header_size * sizeof(size_t)));
        *p = n;
        return p + header_size;
    }

    static inline void deallocate(void *p) noexcept {
        size_t *q = static_cast<size_t *>(p) - header_size;
        ::operator delete(q);
    }
};

};  // namespace asco::base

#endif
