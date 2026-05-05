// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>

#include <asco/core/mm/pool.h>

namespace asco::io {

template<core::mm::allocator Alloc = std::pmr::polymorphic_allocator<>>
class buffer final {
    friend class file;

public:
    explicit buffer() noexcept = default;

    ~buffer() { clear(); }

    buffer(const buffer &) noexcept = delete;
    buffer &operator=(const buffer &) noexcept = delete;

    buffer(buffer &&rhs)
            : m_data{std::move(rhs.m_data)}
            , m_size{rhs.m_size}
            , m_cursor{rhs.m_cursor}
            // 拷贝分配器是刻意的，我们保证 moved-from buffer 依旧可用，分配器不能被移走。
            , m_allocator_storage{rhs.m_allocator_storage} {
        if (rhs.m_allocator_storage && rhs.m_allocator == &*rhs.m_allocator_storage) {
            m_allocator = &*m_allocator_storage;
        } else {
            m_allocator = rhs.m_allocator;
        }
        rhs.m_data = nullptr;
        rhs.clear();
    }

    buffer &operator=(buffer &&rhs) {
        if (this != &rhs) {
            clear();
            m_data = std::move(rhs.m_data);
            m_size = rhs.m_size;
            m_cursor = rhs.m_cursor;
            m_allocator_storage.reset();
            if (rhs.m_allocator_storage) {
                m_allocator_storage.emplace(*rhs.m_allocator_storage);
            }
            if (rhs.m_allocator_storage && rhs.m_allocator == &*rhs.m_allocator_storage) {
                m_allocator = &*m_allocator_storage;
            } else {
                m_allocator = rhs.m_allocator;
            }
            rhs.m_data = nullptr;
            rhs.clear();
        }
        return *this;
    }

    explicit buffer(Alloc &&allocator)
            : m_data{nullptr}
            , m_size{0}
            , m_cursor{0}
            , m_allocator_storage{std::move(allocator)}
            , m_allocator{&*m_allocator_storage} {}

    explicit buffer(Alloc &allocator)
            : m_data{nullptr}
            , m_size{0}
            , m_cursor{0}
            , m_allocator_storage{std::nullopt}
            , m_allocator{&allocator} {}

    explicit buffer(std::size_t size, Alloc &&allocator)
            : m_data{reinterpret_cast<std::byte *>(allocator.allocate_bytes(size))}
            , m_size{size}
            , m_cursor{0}
            , m_allocator_storage{std::move(allocator)}
            , m_allocator{&*m_allocator_storage} {}

    explicit buffer(std::size_t size, Alloc &allocator = core::mm::pmr::get<buffer>())
            : m_data{reinterpret_cast<std::byte *>(allocator.allocate_bytes(size))}
            , m_size{size}
            , m_cursor{0}
            , m_allocator_storage{std::nullopt}
            , m_allocator{&allocator} {}

    explicit buffer(std::string_view string, Alloc &&allocator)
            : m_data{reinterpret_cast<std::byte *>(allocator.allocate_bytes(string.size()))}
            , m_size{string.size()}
            , m_cursor{string.size()}
            , m_allocator_storage{std::move(allocator)}
            , m_allocator{&*m_allocator_storage} {
        std::ranges::copy_n(reinterpret_cast<const std::byte *>(string.begin()), string.size(), m_data);
    }

    explicit buffer(std::string_view string, Alloc &allocator = core::mm::pmr::get<buffer>())
            : m_data{reinterpret_cast<std::byte *>(allocator.allocate_bytes(string.size()))}
            , m_size{string.size()}
            , m_cursor{string.size()}
            , m_allocator_storage{std::nullopt}
            , m_allocator{&allocator} {
        std::ranges::copy_n(reinterpret_cast<const std::byte *>(string.begin()), string.size(), m_data);
    }

    std::size_t size() const noexcept { return m_size; }

    std::byte *data() const noexcept { return m_data; }

    std::size_t cursor() const noexcept { return m_cursor; }

    void reserve(std::size_t size) {
        if (size <= m_size - m_cursor) {
            return;
        }

        std::size_t new_size = m_cursor + size;
        std::byte *new_data = reinterpret_cast<std::byte *>(m_allocator->allocate_bytes(new_size));
        if (m_data) {
            if (m_cursor > 0) {
                std::ranges::copy_n(m_data, m_cursor, new_data);
            }
            m_allocator->deallocate_bytes(m_data, m_size);
        }
        m_data = new_data;
        m_size = new_size;
    }

    std::size_t push(std::span<const std::byte> bytes) {
        auto cursor = m_cursor;
        auto size = try_advance(bytes.size());
        std::ranges::copy(bytes.first(size), m_data + cursor);
        return size;
    }

    std::size_t push(std::string_view string) {
        return push({reinterpret_cast<const std::byte *>(string.data()), string.size()});
    }

    std::size_t push(const buffer &buf) {
        return push(std::span<const std::byte>{buf.data(), buf.m_cursor});
    }

    void clear() {
        if (m_data) {
            m_allocator->deallocate_bytes(m_data, m_size);
        }
        m_data = nullptr;
        m_size = 0;
        m_cursor = 0;
    }

private:
    std::byte *m_data{nullptr};
    std::size_t m_size{0};

    std::size_t m_cursor{0};

    std::optional<Alloc> m_allocator_storage{std::nullopt};
    Alloc *m_allocator{&core::mm::pmr::get<buffer>()};

    std::size_t try_advance(std::size_t count) noexcept {
        if (m_size == 0) {
            m_size = count;
            m_data = reinterpret_cast<std::byte *>(m_allocator->allocate_bytes(m_size));
            m_cursor = count;
            return count;
        } else {
            std::size_t remaining = m_size - m_cursor;
            std::size_t advance_by = std::min(count, remaining);
            m_cursor += advance_by;
            return advance_by;
        }
    }
};

};  // namespace asco::io
