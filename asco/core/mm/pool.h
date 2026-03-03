// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
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

class coroutine_pool {
public:
    coroutine_pool(const coroutine_pool &) = delete;
    coroutine_pool &operator=(const coroutine_pool &) = delete;

    coroutine_pool(coroutine_pool &&) = delete;
    coroutine_pool &operator=(coroutine_pool &&) = delete;

    // `n <= max_object_size - block_unit` 使用 freelist 分配，否则直接通过 pmr 分配
    // needed_size(n) 的最后 8 字节是 `block *` ，用于在 deallocate 时将内存块回收到对应的 freelist 中
    static void *allocate(std::size_t n) noexcept;

    static void deallocate(void *addr, std::size_t n) noexcept;

private:
    static coroutine_pool &get() noexcept;

    static std::size_t needed_size(std::size_t n) noexcept { return ((n + 8) + 7) / 8 * 8; }

    struct object {
        object *next;
        std::size_t length;
    };

    constexpr static std::size_t block_unit = 64;
    constexpr static std::size_t min_object_size = block_unit;
    constexpr static std::size_t max_object_size = 4096;

    constexpr static std::size_t giveback_threshold = 1024;

    coroutine_pool() = default;

    // 对于索引 `i` 的 freelist ，其管理的内存块大小为 `block_unit * (i + 1)`
    std::array<object *, 63> freelist{nullptr};

    // 最小的 block 大小为 4096 ，根据当前命中率动态调整每次分配的 block 大小（ 4096 的 2 的幂倍）
    struct alignas(max_object_size) block {
        std::size_t size;
        std::size_t unit_count;
        block *prev, *next;
        std::size_t allocating_index{0};
        std::atomic_size_t gaveback_unit_count{0};

        alignas(block_unit) struct unit {
            char data[block_unit];
        } units[0];

        block(std::size_t block_count) noexcept;

        void *allocate(std::size_t n) noexcept;
        void deallocate(void *addr, std::size_t n) noexcept;
        bool can_free() const noexcept;

        static std::size_t needed_units(std::size_t n) noexcept { return (n - 1) / block_unit + 1; }
    };

    // 仅由当前线程回收 block
    block *blocks_list{nullptr};
    std::size_t block_allocate_exp{0};

    std::size_t allocate_count{0};
    std::size_t block_miss_count{0};

    void update_block_allocate_exp() noexcept;
};

};  // namespace asco::core::mm
