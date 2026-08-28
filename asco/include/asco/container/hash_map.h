// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <optional>

#include "asco/assert.h"
#include "asco/concepts.h"
#include "asco/macros.h"
#include "asco/types/fuck_void.h"
#include "asco/types/int.h"
#include "asco/types/raw_storage.h"
#include "asco/types/try_move.h"
#include "asco/util/hash.h"

namespace asco::container {

template<typename K>
concept hash_key =
    std::is_default_constructible_v<K> && std::equality_comparable<K> && std::copyable<K> && requires(K k) {
        { std::hash<K>{}(k) } -> std::convertible_to<usize>;
    };

namespace detail {

using concepts::non_void;

template<hash_key K, non_void V>
struct pair_ref {
private:
    const K &m_key;
    V &m_value;

public:
    pair_ref(const K &key, V &value)
            : m_key{key}
            , m_value{value} {}

    const K &key() const { return m_key; }

    std::tuple_element_t<1, pair_ref<K, V>> value() { return m_value; }

    const std::tuple_element_t<1, pair_ref<K, V>> value() const { return m_value; }
};

template<hash_key K, non_void V>
    requires(!std::is_void_v<V>)
struct pair_ref_const {
private:
    const K &m_key;
    const V &m_value;

public:
    pair_ref_const(const K &key, const V &value)
            : m_key{key}
            , m_value{value} {}

    const K &key() const { return m_key; }

    std::tuple_element_t<1, pair_ref_const<K, V>> value() const { return m_value; }
};

template<usize I, hash_key K, non_void V>
std::tuple_element_t<I, pair_ref<K, V>> get(const pair_ref<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else {
        return p.value();
    }
}

template<usize I, hash_key K, non_void V>
std::tuple_element_t<I, pair_ref_const<K, V>> get(const pair_ref_const<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else {
        return p.value();
    }
}

};  // namespace detail

};  // namespace asco::container

template<
    asco::container::hash_key K, asco::concepts::non_void V, template<class...> typename TQual,
    template<class...> typename UQual>
struct std::basic_common_reference<
    asco::container::detail::pair_ref<K, V>, asco::container::detail::pair_ref<K, V>, TQual, UQual> {
    using type = const asco::container::detail::pair_ref<K, V> &;
};

template<
    asco::container::hash_key K, asco::concepts::non_void V, template<class...> typename TQual,
    template<class...> typename UQual>
struct std::basic_common_reference<
    asco::container::detail::pair_ref_const<K, V>, asco::container::detail::pair_ref_const<K, V>, TQual,
    UQual> {
    using type = const asco::container::detail::pair_ref_const<K, V> &;
};

namespace asco::container {

template<hash_key K, typename V>
class hash_map {
    struct slot {
        enum class state {
            empty,
            filled,
            tombstone,
        } st;
        K key;
        [[ASCO_NO_UNIQUE_ADDRESS]] types::raw_storage<V> value;
    };

    constexpr static usize default_slots_size = 64;

    constexpr static usize load_factor_numerator = 3;
    constexpr static usize load_factor_denominator = 4;

    enum class try_insert_result {
        succeeded,
        rehash_needed,
        duplicated,
    };

public:
    class iterator {
        friend class hash_map;

        using mut_value_type = types::fuck_void_or_else<V, detail::pair_ref<K, types::fuck_void<V>>, K>;
        using const_value_type =
            types::fuck_void_or_else<V, detail::pair_ref_const<K, types::fuck_void<V>>, K>;

    public:
        using difference_type = isize;
        using value_type = mut_value_type;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        iterator() noexcept = default;

        ~iterator() {
            if (m_cache_valid) {
                m_cache.destroy();
            }
        }

        value_type &operator*() const noexcept {
            ASCO_ASSERT(validative_check() && end_check());

            if (m_cache_valid) {
                return *m_cache.get();
            }
            auto &sl = m_map->m_slots.at(m_index.value());
            if constexpr (std::is_void_v<V>) {
                m_cache.emplace(sl.key);
            } else {
                m_cache.emplace(sl.key, *sl.value.get());
            }
            m_cache_valid = true;
            return *m_cache.get();
        }

        iterator &operator++() noexcept {
            ASCO_ASSERT(validative_check() && end_check());

            m_index.value() += 1;
            m_counter += 1;
            if (m_cache_valid) {
                m_cache.destroy();
                m_cache_valid = false;
            }
            next_filled();
            return *this;
        }

        iterator operator++(int) noexcept {
            ASCO_ASSERT(validative_check() && end_check());

            iterator res = *this;
            ++(*this);
            return res;
        }

        iterator(const iterator &other) noexcept
                : m_map{other.m_map}
                , m_generation{other.m_generation}
                , m_counter{other.m_counter}
                , m_index{other.m_index} {}

        iterator &operator=(const iterator &other) noexcept {
            if (this == &other) {
                return *this;
            }
            if (m_cache_valid) {
                m_cache.destroy();
            }
            m_map = other.m_map;
            m_generation = other.m_generation;
            m_counter = other.m_counter;
            m_index = other.m_index;
            m_cache_valid = false;
            return *this;
        }

        iterator(iterator &&other) noexcept
                : m_map{other.m_map}
                , m_generation{other.m_generation}
                , m_counter{other.m_counter}
                , m_index{other.m_index} {
            other.m_index = std::nullopt;
            other.m_counter = 0;
            if (other.m_map) {
                other.m_generation = other.m_map->m_generation + 1;
                other.m_map = nullptr;
            } else {
                other.m_generation = other.m_generation + 1;
            }
        }

        iterator &operator=(iterator &&other) noexcept {
            if (this == &other) {
                return *this;
            }
            if (m_cache_valid) {
                m_cache.destroy();
            }
            m_map = other.m_map;
            m_generation = other.m_generation;
            m_counter = other.m_counter;
            m_index = other.m_index;
            m_cache_valid = false;
            other.m_index = std::nullopt;
            other.m_counter = 0;
            if (other.m_map) {
                other.m_generation = other.m_map->m_generation + 1;
                other.m_map = nullptr;
            } else {
                other.m_generation = other.m_generation + 1;
            }
            return *this;
        }

        bool operator==(const iterator &rhs) const noexcept {
            ASCO_ASSERT(validative_check() && rhs.validative_check() && m_map == rhs.m_map);

            return m_index == rhs.m_index;
        }

        friend difference_type operator-(const iterator &lhs, const iterator &rhs) noexcept {
            ASCO_ASSERT(lhs.validative_check() && rhs.validative_check() && lhs.m_map == rhs.m_map);

            return static_cast<difference_type>(lhs.m_counter) - static_cast<difference_type>(rhs.m_counter);
        }

    private:
        iterator(hash_map &map, usize generation, bool end = false) noexcept
                : m_map{&map}
                , m_generation{generation}
                , m_counter{end ? map.m_load : 0}
                , m_index{end ? std::optional<usize>{} : std::optional<usize>{0}} {
            if (!end) {
                next_filled();
            }
        }

        void next_filled() noexcept {
            if (!m_index.has_value()) {
                return;
            }
            if (m_index.value() == m_map->m_slots.size()) {
                m_index = std::nullopt;
                return;
            }
            while (m_index.value() < m_map->m_slots.size()
                   && m_map->m_slots.at(m_index.value()).st != slot::state::filled) {
                m_index.value() += 1;
            }
            if (m_index.value() == m_map->m_slots.size()) {
                m_index = std::nullopt;
            }
        }

        bool validative_check() const noexcept {
            if (!m_map) {
                return false;
            }
            if (m_map->m_generation != m_generation) [[unlikely]] {
                return false;
            }
            return true;
        }

        bool end_check() const noexcept { return m_index.has_value(); }

        hash_map *m_map;
        usize m_generation;
        usize m_counter{0};
        std::optional<usize> m_index;

        [[ASCO_NO_UNIQUE_ADDRESS]] mutable types::raw_storage<mut_value_type> m_cache;
        mutable bool m_cache_valid{false};
    };

    class iterator_const {
        friend class hash_map;

        using stored_value_type =
            types::fuck_void_or_else<V, detail::pair_ref_const<K, types::fuck_void<V>>, K>;

    public:
        using difference_type = isize;
        using value_type = stored_value_type;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        iterator_const() noexcept = default;

        ~iterator_const() {
            if (m_cache_valid) {
                m_cache.destroy();
            }
        }

        const value_type &operator*() const noexcept {
            ASCO_ASSERT(validative_check() && end_check());

            if (m_cache_valid) {
                return *m_cache.get();
            }
            auto &sl = m_map->m_slots.at(m_index.value());
            if constexpr (std::is_void_v<V>) {
                m_cache.emplace(sl.key);
            } else {
                m_cache.emplace(sl.key, *sl.value.get());
            }
            m_cache_valid = true;
            return *m_cache.get();
        }

        iterator_const &operator++() noexcept {
            ASCO_ASSERT(validative_check() && end_check());

            m_index.value() += 1;
            m_counter += 1;
            if (m_cache_valid) {
                m_cache.destroy();
                m_cache_valid = false;
            }
            next_filled();
            return *this;
        }

        iterator_const operator++(int) noexcept {
            ASCO_ASSERT(validative_check() && end_check());

            iterator_const res = *this;
            ++(*this);
            return res;
        }

        iterator_const(const iterator_const &other) noexcept
                : m_map{other.m_map}
                , m_generation{other.m_generation}
                , m_counter{other.m_counter}
                , m_index{other.m_index} {}

        iterator_const &operator=(const iterator_const &other) noexcept {
            if (this == &other) {
                return *this;
            }
            if (m_cache_valid) {
                m_cache.destroy();
            }
            m_map = other.m_map;
            m_generation = other.m_generation;
            m_counter = other.m_counter;
            m_index = other.m_index;
            m_cache_valid = false;
            return *this;
        }

        iterator_const(iterator_const &&other) noexcept
                : m_map{other.m_map}
                , m_generation{other.m_generation}
                , m_counter{other.m_counter}
                , m_index{other.m_index} {
            other.m_index = std::nullopt;
            other.m_counter = 0;
            if (other.m_map) {
                other.m_generation = other.m_map->m_generation + 1;
                other.m_map = nullptr;
            } else {
                other.m_generation = other.m_generation + 1;
            }
        }

        iterator_const &operator=(iterator_const &&other) noexcept {
            if (this == &other) {
                return *this;
            }
            if (m_cache_valid) {
                m_cache.destroy();
            }
            m_map = other.m_map;
            m_generation = other.m_generation;
            m_counter = other.m_counter;
            m_index = other.m_index;
            m_cache_valid = false;
            other.m_index = std::nullopt;
            other.m_counter = 0;
            if (other.m_map) {
                other.m_generation = other.m_map->m_generation + 1;
                other.m_map = nullptr;
            } else {
                other.m_generation = other.m_generation + 1;
            }
            return *this;
        }

        bool operator==(const iterator_const &rhs) const noexcept {
            ASCO_ASSERT(validative_check() && rhs.validative_check() && m_map == rhs.m_map);

            return m_index == rhs.m_index;
        }

        friend difference_type operator-(const iterator_const &lhs, const iterator_const &rhs) noexcept {
            ASCO_ASSERT(lhs.validative_check() && rhs.validative_check() && lhs.m_map == rhs.m_map);

            return static_cast<difference_type>(lhs.m_counter) - static_cast<difference_type>(rhs.m_counter);
        }

    private:
        iterator_const(const hash_map &map, usize generation, bool end = false) noexcept
                : m_map{&map}
                , m_generation{generation}
                , m_counter{end ? map.m_load : 0}
                , m_index{end ? std::optional<usize>{} : std::optional<usize>{0}} {
            if (!end) {
                next_filled();
            }
        }

        void next_filled() noexcept {
            if (!m_index.has_value()) {
                return;
            }
            if (m_index.value() == m_map->m_slots.size()) {
                m_index = std::nullopt;
                return;
            }
            while (m_index.value() < m_map->m_slots.size()
                   && m_map->m_slots.at(m_index.value()).st != slot::state::filled) {
                m_index.value() += 1;
            }
            if (m_index.value() == m_map->m_slots.size()) {
                m_index = std::nullopt;
            }
        }

        bool validative_check() const noexcept {
            if (!m_map) {
                return false;
            }
            if (m_map->m_generation != m_generation) [[unlikely]] {
                return false;
            }
            return true;
        }

        bool end_check() const noexcept { return m_index.has_value(); }

        const hash_map *m_map;
        usize m_generation;
        usize m_counter{0};
        std::optional<usize> m_index;

        [[ASCO_NO_UNIQUE_ADDRESS]] mutable types::raw_storage<value_type> m_cache;
        mutable bool m_cache_valid{false};
    };

    hash_map() = default;

    iterator begin() noexcept { return {*this, m_generation}; }
    iterator_const begin() const noexcept { return {*this, m_generation}; }

    iterator end() noexcept { return {*this, m_generation, true}; }
    iterator_const end() const noexcept { return {*this, m_generation, true}; }

    class view_type : public std::ranges::view_interface<view_type> {
    public:
        view_type() noexcept = default;
        explicit view_type(hash_map *m) noexcept
                : m_map{m} {}

        iterator begin() noexcept { return m_map->begin(); }
        iterator end() noexcept { return m_map->end(); }

    private:
        hash_map *m_map{nullptr};
    };

    class view_type_const : public std::ranges::view_interface<view_type_const> {
    public:
        view_type_const() noexcept = default;
        explicit view_type_const(const hash_map *m) noexcept
                : m_map{m} {}

        iterator_const begin() const noexcept { return m_map->begin(); }
        iterator_const end() const noexcept { return m_map->end(); }

    private:
        const hash_map *m_map{nullptr};
    };

    view_type view() noexcept { return view_type{this}; }
    view_type_const view() const noexcept { return view_type_const{this}; }

    usize size() const noexcept { return m_load; }

    bool is_empty() const noexcept { return m_load == 0; }

    usize capacity() const noexcept { return m_slots.size(); }

    void rehash() {
        usize size = m_slots.size();

        bool shoud_extend = m_load * load_factor_denominator > load_factor_numerator * size;
        bool shoud_shrink = m_load * load_factor_denominator * 4 < load_factor_numerator * size;
        if (!shoud_extend && !shoud_shrink) {
            return;
        }

        auto old_slots = std::vector<slot>(shoud_extend ? size * 2 : size / 2);
        m_slots.swap(old_slots);
        m_load = 0;
        for (slot &sl : old_slots) {
            if (sl.st != slot::state::filled) {
                continue;
            }

            if constexpr (std::is_void_v<V>) {
                try_insert(sl.key, std::monostate{});
            } else {
                try_insert(sl.key, try_move(*sl.value.get()));
            }
        }

        m_generation += 1;
    }

    template<typename... Args>
    bool emplace(const K &key, Args &&...args) {
        while (true) {
            switch (try_insert(key, std::forward<Args>(args)...)) {
            case try_insert_result::succeeded:
                m_generation += 1;
                return true;
            case try_insert_result::rehash_needed: {
                rehash();
            } break;
            case try_insert_result::duplicated:
                return false;
            }
        }
    }

    bool insert(const K &key, try_move_t<types::fuck_void<V>> v) {
        return emplace(key, std::forward<decltype(v)>(v));
    }

    bool insert(const K &key)
        requires concepts::is_void<V>
    {
        return insert(key, std::monostate{});
    }

    std::optional<types::fuck_void<V>> remove(const K &key) noexcept(
        types::is_nothrow_try_movable_v<types::fuck_void<V>>
        && std::is_nothrow_destructible_v<types::fuck_void<V>>)
        requires(types::is_try_movable_v<types::fuck_void<V>>)
    {
        usize size = m_slots.size();
        usize index = m_hasher(key);
        usize step = probe_step(index, size);
        index %= size;

        for (usize i = 0; i < size; ++i) {
            slot &s = m_slots[probe_index(index, step, size, i)];

            switch (s.st) {
            case slot::state::empty: {
                return std::nullopt;
            }
            case slot::state::filled: {
                if (s.key != key) {
                    continue;
                }

                if constexpr (concepts::non_void<V>) {
                    std::optional<V> res{try_move(*s.value.get())};
                    s.st = slot::state::tombstone;
                    s.value.destroy();
                    m_load -= 1;
                    m_generation += 1;
                    return res;
                } else {
                    s.st = slot::state::tombstone;
                    m_load -= 1;
                    m_generation += 1;
                    return std::monostate{};
                }
            } break;
            }
        }

        return std::nullopt;
    }

    bool remove(const K &key) noexcept(std::is_nothrow_destructible_v<types::fuck_void<V>>)
        requires(!types::is_try_movable_v<types::fuck_void<V>>)
    {
        usize size = m_slots.size();
        usize index = m_hasher(key);
        usize step = probe_step(index, size);
        index %= size;

        for (usize i = 0; i < size; ++i) {
            slot &s = m_slots[probe_index(index, step, size, i)];

            switch (s.st) {
            case slot::state::empty: {
                return false;
            }
            case slot::state::filled: {
                if (s.key != key) {
                    continue;
                }

                s.st = slot::state::tombstone;
                if constexpr (concepts::non_void<V>) {
                    s.value.destroy();
                } else {
                    s.st = slot::state::tombstone;
                }
                m_load -= 1;
                m_generation += 1;
                return true;
            } break;
            }
        }

        return false;
    }

    V *get(const K &key) noexcept {
        usize size = m_slots.size();
        usize index = m_hasher(key);
        usize step = probe_step(index, size);
        index %= size;

        for (usize i = 0; i < size; ++i) {
            slot &s = m_slots[probe_index(index, step, size, i)];

            switch (s.st) {
            case slot::state::empty: {
                return nullptr;
            }
            case slot::state::filled: {
                if (s.key != key) {
                    continue;
                }

                return s.value.get();
            } break;
            }
        }

        return nullptr;
    }

    const V *get(const K &key) const noexcept {
        usize size = m_slots.size();
        usize index = m_hasher(key);
        usize step = probe_step(index, size);
        index %= size;

        for (usize i = 0; i < size; ++i) {
            const slot &s = m_slots[probe_index(index, step, size, i)];

            switch (s.st) {
            case slot::state::empty: {
                return nullptr;
            }
            case slot::state::filled: {
                if (s.key != key) {
                    continue;
                }

                return s.value.get();
            } break;
            }
        }

        return nullptr;
    }

private:
    static usize probe_step(usize index, usize size) noexcept {
        usize step = util::mix64(index) % size;
        return step | 1;
    }

    static usize probe_index(usize index, usize step, usize size, usize probe) noexcept {
        return (index + probe * step) % size;
    }

    template<typename... Args>
    try_insert_result
    try_insert(const K &key, Args &&...args) noexcept(std::is_nothrow_constructible_v<V, Args...>) {
        usize size = m_slots.size();

        if (m_load * load_factor_denominator > load_factor_numerator * size) [[unlikely]] {
            return try_insert_result::rehash_needed;
        }

        usize index = m_hasher(key);
        usize step = probe_step(index, size);
        index %= size;

        std::optional<usize> insert_location;

        for (usize i = 0; i < size; ++i) {
            usize location = probe_index(index, step, size, i);
            slot &s = m_slots[location];

            switch (s.st) {
            case slot::state::tombstone: {
                if (!insert_location.has_value()) {
                    insert_location = location;
                }
            } break;
            case slot::state::empty: {
                insert_location = location;
            } break;
            case slot::state::filled: {
                if (s.key == key) {
                    return try_insert_result::duplicated;
                }
            }
            }

            if (insert_location.has_value()) {
                break;
            }
        }
        if (!insert_location.has_value()) [[unlikely]] {
            std::unreachable();
        }

        slot &s = m_slots[insert_location.value()];
        if constexpr (concepts::non_void<V>) {
            s.value.emplace(std::forward<Args>(args)...);
        }
        s.key = key;

        s.st = slot::state::filled;
        m_load += 1;
        return try_insert_result::succeeded;
    }

    [[ASCO_NO_UNIQUE_ADDRESS]] std::hash<K> m_hasher{};
    std::vector<slot> m_slots{64};
    usize m_load{0};

    usize m_generation{0};
};

template<hash_key K>
using hash_set = hash_map<K, void>;

};  // namespace asco::container

template<asco::container::hash_key K, asco::concepts::non_void V>
struct std::tuple_size<asco::container::detail::pair_ref<K, V>> : std::integral_constant<std::size_t, 2> {};

template<asco::container::hash_key K, asco::concepts::non_void V>
struct std::tuple_size<asco::container::detail::pair_ref_const<K, V>>
        : std::integral_constant<std::size_t, 2> {};

template<asco::container::hash_key K, asco::concepts::non_void V>
struct std::tuple_element<0, asco::container::detail::pair_ref<K, V>> {
    using type = const K &;
};

template<asco::container::hash_key K, asco::concepts::non_void V>
struct std::tuple_element<1, asco::container::detail::pair_ref<K, V>> {
    using type = V &;
};

template<asco::container::hash_key K, asco::concepts::non_void V>
struct std::tuple_element<0, asco::container::detail::pair_ref_const<K, V>> {
    using type = const K &;
};

template<asco::container::hash_key K, asco::concepts::non_void V>
struct std::tuple_element<1, asco::container::detail::pair_ref_const<K, V>> {
    using type = const V &;
};
