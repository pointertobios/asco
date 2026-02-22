// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <asco/util/consts.h>
#include <asco/util/raw_storage.h>
#include <asco/util/types.h>

// MPMC 环形队列
// 无阻塞，立即返回

namespace asco::concurrency::ring_queue {

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
class sender;

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
class receiver;

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
std::pair<sender<T, Cap>, receiver<T, Cap>> create();

namespace detail {

enum class slot_state : std::uint8_t {  //
    empty,
    constructing,
    filled,
    deconstructing,
    exception
};

};  // namespace detail

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
class storage final {
    friend class sender<T, Cap>;
    friend class receiver<T, Cap>;

    using slot_state = detail::slot_state;

public:
    static constexpr std::size_t size = Cap + 1;

    storage() {
        for (auto &arr : slot_states) {
            for (auto &s : arr) {
                s = slot_state::empty;
            }
        }
    }

    ~storage() {
        for (std::size_t i = 0; i < size; ++i) {
            if (state_at(i).load(std::memory_order::acquire) == slot_state::filled) {
                at(i).get()->~T();
            }
        }
    }

private:
    static constexpr std::size_t null_index = std::numeric_limits<std::size_t>::max();

    static constexpr std::size_t cache_line_cap = util::cacheline / sizeof(util::raw_storage<T>);
    static constexpr std::size_t slot_pack_cap = cache_line_cap ? cache_line_cap : 1;

    using slot_pack_type = std::conditional_t<
        cache_line_cap, std::array<util::raw_storage<T>, slot_pack_cap>, util::raw_storage<T>>;

    struct alignas(util::cacheline) slot_pack : public slot_pack_type {};

    struct alignas(util::cacheline) states_pack
            : public std::array<std::atomic<slot_state>, util::cacheline> {};

    static constexpr std::size_t buffer_cap = (size + slot_pack_cap - 1) / slot_pack_cap;

    util::raw_storage<T> &at(std::size_t index) noexcept {
        if constexpr (!cache_line_cap) {
            return slots[index];
        } else {
            const std::size_t pack_index = index % buffer_cap;
            const std::size_t slot_index = index / buffer_cap;
            return slots[pack_index][slot_index];
        }
    }

    static constexpr std::size_t slot_states_cap = (size + util::cacheline - 1) / util::cacheline;

    std::atomic<slot_state> &state_at(std::size_t index) noexcept {
        const std::size_t pack_index = index % slot_states_cap;
        const std::size_t slot_index = index / slot_states_cap;
        return slot_states[pack_index][slot_index];
    }

    alignas(util::cacheline) std::atomic_size_t head{0};
    alignas(util::cacheline) std::atomic_size_t tail{0};

    alignas(util::cacheline) std::array<slot_pack, buffer_cap> slots;

    alignas(util::cacheline) std::array<states_pack, slot_states_cap> slot_states{};
};

template<std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
class storage<void, Cap> final {
    friend class sender<void, Cap>;
    friend class receiver<void, Cap>;

    static constexpr std::size_t size = 0;

public:
    storage() = default;
    ~storage() = default;

private:
    static constexpr std::size_t null_index = std::numeric_limits<std::size_t>::max();

    std::atomic_size_t count{0};
};

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
class sender final {
    friend std::pair<sender<T, Cap>, receiver<T, Cap>> create<T, Cap>();

    static constexpr std::size_t storage_size = storage<T, Cap>::size;

    using storage = storage<T, Cap>;

    using slot_state = detail::slot_state;

public:
    sender() = default;

    sender(const sender &) noexcept = default;
    sender &operator=(const sender &) noexcept = default;

    sender(sender &&) noexcept = default;
    sender &operator=(sender &&) noexcept = default;

    std::optional<T> try_send(util::types::monostate_if_void<T> val)
        requires(!std::is_void_v<T>)
    {
        std::size_t current_head;
        std::size_t current_tail;

        while (true) {
            current_head = m_stor->head.load(std::memory_order::acquire);
            current_tail = m_stor->tail.load(std::memory_order::acquire);

            if ((current_tail + 1) % storage_size == current_head) {
                return std::move(val);
            }

            if (current_tail == m_stor->tail.load(std::memory_order::acquire)
                && current_head == m_stor->head.load(std::memory_order::acquire)) {
                auto s = slot_state::empty;
                auto &st = m_stor->state_at(current_tail);
                if (!st.compare_exchange_strong(
                        s, slot_state::constructing, std::memory_order::acq_rel,
                        std::memory_order::relaxed)) {
                    continue;
                }
            } else {
                continue;
            }

            if (auto t = current_tail; !m_stor->tail.compare_exchange_strong(
                    t, (current_tail + 1) % storage_size, std::memory_order::release,
                    std::memory_order::relaxed)) {
                m_stor->state_at(current_tail).store(slot_state::empty, std::memory_order::release);
                continue;
            }

            break;
        }

        try {
            new (m_stor->at(current_tail).get()) T{std::move(val)};

            m_stor->state_at(current_tail).store(slot_state::filled, std::memory_order::release);
            return std::nullopt;
        } catch (...) {
            m_stor->state_at(current_tail).store(slot_state::exception, std::memory_order::release);
            std::rethrow_exception(std::current_exception());
        }
    }

    bool try_send()
        requires(std::is_void_v<T>)
    {
        std::size_t current;
        do {
            current = m_stor->count.load(std::memory_order::acquire);
            if (current == Cap) {
                return true;
            }
        } while (!m_stor->count.compare_exchange_strong(
            current, current + 1, std::memory_order::release, std::memory_order::relaxed));
        return false;
    }

private:
    sender(std::shared_ptr<storage> stor)
            : m_stor{stor} {}

    std::shared_ptr<storage> m_stor;
};

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
class receiver final {
    friend std::pair<sender<T, Cap>, receiver<T, Cap>> create<T, Cap>();

    static constexpr std::size_t storage_size = storage<T, Cap>::size;

    using storage = storage<T, Cap>;

    static constexpr std::size_t null_index = storage::null_index;

    using slot_state = detail::slot_state;

public:
    receiver() = default;

    receiver(const receiver &) noexcept = default;
    receiver &operator=(const receiver &) noexcept = default;

    receiver(receiver &&) noexcept = default;
    receiver &operator=(receiver &&) noexcept = default;

    std::optional<util::types::monostate_if_void<T>> try_recv()
        requires(!std::is_void_v<T>)
    {
        std::size_t current_head;
        std::size_t current_tail;

        while (true) {
            current_head = m_stor->head.load(std::memory_order::acquire);
            current_tail = m_stor->tail.load(std::memory_order::acquire);

        start_race:
            if (current_head == current_tail) {
                return std::nullopt;
            }

            if (current_head == m_stor->head.load(std::memory_order::acquire)) {
                auto s = slot_state::filled;
                auto &st = m_stor->state_at(current_head);
                if (!st.compare_exchange_strong(
                        s, slot_state::deconstructing, std::memory_order::acquire,
                        std::memory_order::relaxed)) {
                    if (s == slot_state::exception) {
                        m_stor->state_at(current_head).store(slot_state::empty, std::memory_order::release);
                        auto h = current_head;
                        m_stor->head.compare_exchange_strong(
                            h, (current_head + 1) % storage_size, std::memory_order::release,
                            std::memory_order::relaxed);
                        continue;
                    } else if (s == slot_state::constructing) {
                        return std::nullopt;
                    }
                    current_head = (current_head + 1) % storage_size;
                    goto start_race;
                }
            } else {
                continue;
            }

            for (  //
                auto h = current_head; !m_stor->head.compare_exchange_strong(
                    h, (current_head + 1) % storage_size, std::memory_order::release,
                    std::memory_order::relaxed);
                h = current_head)
                ;

            break;
        }

        std::exception_ptr e;
        std::optional<T> res;

        auto &slot = m_stor->at(current_head);
        try {
            res = std::move(*slot.get());
            slot.get()->~T();
        } catch (...) { e = std::current_exception(); }

        m_stor->state_at(current_head).store(slot_state::empty, std::memory_order::release);

        if (e) {
            std::rethrow_exception(e);
        } else if (res.has_value()) {
            return std::move(*res);
        } else {
            std::unreachable();
        }
    }

    bool try_recv()
        requires(std::is_void_v<T>)
    {
        std::size_t current;
        do {
            current = m_stor->count.load(std::memory_order::acquire);
            if (current == 0) {
                return false;
            }
        } while (!m_stor->count.compare_exchange_strong(
            current, current - 1, std::memory_order::release, std::memory_order::relaxed));
        return true;
    }

private:
    receiver(std::shared_ptr<storage> stor)
            : m_stor{stor} {}

    std::shared_ptr<storage> m_stor;
};

template<util::types::move_secure T, std::size_t Cap>
    requires(Cap != std::numeric_limits<std::size_t>::max())
std::pair<sender<T, Cap>, receiver<T, Cap>> create() {
    auto stor = std::make_shared<storage<T, Cap>>();
    return std::make_pair(sender<T, Cap>{stor}, receiver<T, Cap>{stor});
}

};  // namespace asco::concurrency::ring_queue
