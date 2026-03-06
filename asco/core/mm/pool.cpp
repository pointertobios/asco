// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/mm/pool.h>

#include <atomic>

namespace asco::core::mm {

coroutine_pool &coroutine_pool::get() noexcept {
    thread_local coroutine_pool _pool{};
    return _pool;
}

void *coroutine_pool::allocate(std::size_t n) noexcept {
    n = needed_size(n);

    if (n > max_object_size - block_unit) [[unlikely]] {
        try {
            return pmr::get<coroutine_pmr_tag>().allocate_bytes(n);
        } catch (const std::bad_alloc &) { return nullptr; }
    }

    auto &self = get();

    self.allocate_count++;

    std::size_t index = (n - 1) / block_unit;
    if (auto obj = self.freelist[index]) {
        self.freelist[index] = obj->next;

        self.update_block_allocate_exp();

        return obj;
    }

    auto blist = self.blocks_list;
    while (blist) {
        if (auto ptr = blist->allocate(n)) {
            return ptr;
        }
        if (blist->can_free()) {
            auto to_free = blist;
            blist = blist->next;
            if (to_free->prev) {
                to_free->prev->next = to_free->next;
            } else {
                self.blocks_list = to_free->next;
            }
            if (to_free->next) {
                to_free->next->prev = to_free->prev;
            }
            pmr::get_local().deallocate_bytes(to_free, to_free->size);
        } else {
            blist = blist->next;
        }
    }

    self.block_miss_count++;
    self.update_block_allocate_exp();

    auto block_count = 1ull << self.block_allocate_exp;
    block *new_block;
    try {
        new_block = reinterpret_cast<block *>(pmr::get_local().allocate_bytes(max_object_size * block_count));
    } catch (const std::bad_alloc &) { return nullptr; }
    new (new_block) block(block_count);

    new_block->prev = nullptr;
    new_block->next = self.blocks_list;
    if (self.blocks_list) {
        self.blocks_list->prev = new_block;
        self.blocks_list = new_block;
    } else {
        self.blocks_list = new_block;
    }

    return new_block->allocate(n);
}

void coroutine_pool::deallocate(void *addr, std::size_t n) noexcept {
    n = needed_size(n);

    if (n > max_object_size - block_unit) [[unlikely]] {
        pmr::get<coroutine_pmr_tag>().deallocate_bytes(addr, n);
        return;
    }

    auto &self = get();

    std::size_t index = (n - 1) / block_unit;

    if (self.freelist[index] == nullptr || self.freelist[index]->length <= giveback_threshold) {
        auto obj = reinterpret_cast<object *>(addr);
        obj->next = self.freelist[index];
        obj->length = self.freelist[index] ? (self.freelist[index]->length + 1) : 1;
        self.freelist[index] = obj;
        return;
    }

    auto this_block = reinterpret_cast<block *>(reinterpret_cast<std::size_t *>(addr)[n / 8 - 1]);
    this_block->deallocate(addr, n);
}

void coroutine_pool::update_block_allocate_exp() noexcept {
    if (block_miss_count == 0) {
        return;
    }
    if (allocate_count / block_miss_count <= 2) {
        block_allocate_exp++;
    } else {
        block_allocate_exp = block_allocate_exp == 0 ? 0 : block_allocate_exp - 1;
    }
}

coroutine_pool::block::block(std::size_t block_count) noexcept {
    this->size = max_object_size * block_count;
    this->unit_count = size / block_unit - 1;
}

void *coroutine_pool::block::allocate(std::size_t n) noexcept {
    std::size_t units_needed = needed_units(n);
    if (allocating_index + units_needed > unit_count) {
        return nullptr;
    }
    auto res = reinterpret_cast<std::size_t *>(units[allocating_index].data);
    res[n / 8 - 1] = reinterpret_cast<std::size_t>(this);
    allocating_index += units_needed;
    return res;
}

void coroutine_pool::block::deallocate(void *, std::size_t n) noexcept {
    std::size_t units = needed_units(n);
    gaveback_unit_count.fetch_add(units);
}

bool coroutine_pool::block::can_free() const noexcept {
    return gaveback_unit_count.load(std::memory_order::acquire) == unit_count;
}

};  // namespace asco::core::mm
