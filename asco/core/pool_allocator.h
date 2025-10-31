// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <memory_resource>
#include <mutex>

#include <asco/utils/memory_slot.h>

namespace asco::core::mm {

template<typename T>
inline std::pmr::synchronized_pool_resource &default_pool() {
    static utils::memory_slot<std::pmr::synchronized_pool_resource> pool_slot;
    static std::once_flag flag;
    std::call_once(flag, [] {
        // We use neverdrop flag here, because we should ensure those memory never be released during the
        // program lifetime.
        pool_slot.emplace(true);
    });
    return pool_slot.get();
}

template<typename T>
inline std::pmr::unsynchronized_pool_resource &local_pool() {
    static utils::memory_slot<std::pmr::unsynchronized_pool_resource> pool_slot;
    static std::once_flag flag;
    std::call_once(flag, [] {
        // We use neverdrop flag here, because we should ensure those memory never be released during the
        // program lifetime.
        pool_slot.emplace(true);
    });
    return pool_slot.get();
}

template<typename T>
inline std::pmr::polymorphic_allocator<T> allocator(std::pmr::synchronized_pool_resource &pool) {
    return std::pmr::polymorphic_allocator<T>{&pool};
}

template<typename T>
inline std::pmr::polymorphic_allocator<T> allocator(std::pmr::unsynchronized_pool_resource &pool) {
    return std::pmr::polymorphic_allocator<T>{&pool};
}

};  // namespace asco::core::mm
