// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <memory_resource>
#include <mutex>

#include <asco/util/raw_storage.h>

namespace asco::core::mm {

struct coroutine_pmr_tag {};

namespace pmr {

template<typename = void>
inline std::pmr::polymorphic_allocator<> &get() noexcept {
    static util::raw_storage<std::pmr::synchronized_pool_resource> pool_storage;
    static std::once_flag flag;
    std::call_once(flag, [] { new (pool_storage.get()) std::pmr::synchronized_pool_resource{}; });
    static std::pmr::polymorphic_allocator<> alloc{pool_storage.get()};
    return alloc;
}

template<typename = void>
inline std::pmr::polymorphic_allocator<> &get_local() noexcept {
    static thread_local util::raw_storage<std::pmr::unsynchronized_pool_resource> pool_storage;
    static thread_local std::once_flag flag;
    std::call_once(flag, [] { new (pool_storage.get()) std::pmr::unsynchronized_pool_resource{}; });
    static thread_local std::pmr::polymorphic_allocator<> alloc{pool_storage.get()};
    return alloc;
}

};  // namespace pmr

template<typename Alloc>
concept allocator = requires {
    typename Alloc::value_type;
    requires std::same_as<typename Alloc::value_type, typename std::allocator_traits<Alloc>::value_type>;
};

};  // namespace asco::core::mm
