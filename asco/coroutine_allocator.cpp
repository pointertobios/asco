// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/coroutine_allocator.h>

namespace asco::base {

thread_local std::unordered_map<size_t, core::slub::object<size_t> *> coroutine_allocator::freelist;

void *coroutine_allocator::allocate(std::size_t n) noexcept {
    if (auto it = freelist.find(n); it != freelist.end()) {
        auto *p = it->second;
        if (!p->next)
            freelist.erase(it);
        else
            it->second = p->next;
        return p->obj();
    }

    auto *p = static_cast<size_t *>(::operator new(n + header_size));
    *p = n;
    return p + (header_size / sizeof(size_t));
}

void coroutine_allocator::deallocate(void *p) noexcept {
    auto obj = core::slub::object<size_t>::from(static_cast<size_t *>(p));
    size_t *q = static_cast<size_t *>(p) - (header_size / sizeof(size_t));
    size_t n = *q;
    if (auto it = freelist.find(n), end = freelist.end(); it != end && it->second->len >= freelist_max) {
        freelist.erase(it);
        ::operator delete(q);
        return;
    } else if (it != end) {
        obj->next = it->second;
        obj->len = it->second->len + 1;
        it->second = obj;
        return;
    } else {
        obj->next = nullptr;
        obj->len = 1;
        freelist[n] = obj;
        return;
    }
}

};  // namespace asco::base
