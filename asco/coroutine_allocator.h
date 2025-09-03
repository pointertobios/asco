// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_COROUTINE_ALLOCATOR_H
#define ASCO_COROUTINE_ALLOCATOR_H 1

#include <unordered_map>

#include <asco/core/slub.h>
#include <asco/utils/pubusing.h>

namespace asco::base {

using namespace types;

struct coroutine_allocator {
public:
    constexpr static size_t header_size = 2 * sizeof(size_t);

    static void *allocate(std::size_t n) noexcept;
    static void deallocate(void *p) noexcept;

private:
    thread_local static std::unordered_map<size_t, core::slub::object<size_t> *> freelist;
    constexpr static size_t freelist_max = 1024;
};

};  // namespace asco::base

#endif
