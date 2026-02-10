// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <iterator>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include <asco/concurrency/concurrency.h>
#include <asco/panic.h>
#include <asco/util/raw_storage.h>
#include <asco/util/types.h>

namespace asco::concurrency {

namespace detail {

enum class bucket_state : std::uint8_t {
    empty,
    constructing,
    filled,
    guard_protecting,  // 存在性守卫，仅保证键值对不被析构
    destructing,
    tombstone
};

};  // namespace detail

// 并发哈希表
template<util::types::hash_key K, util::types::move_secure V>
struct pair_ref {
private:
    const K &m_key;
    V *m_value;

public:
    pair_ref(const K &key)
        requires(std::is_void_v<V>)
            : m_key{key}
            , m_value{nullptr} {}

    pair_ref(const K &key, util::types::monostate_if_void<V> &value)
        requires(!std::is_void_v<V>)
            : m_key{key}
            , m_value{&value} {}

    const K &key() const { return m_key; }

    util::types::monostate_if_void<V> &value()
        requires(!std::is_void_v<V>)
    {
        return *m_value;
    }

    const util::types::monostate_if_void<V> &value() const
        requires(!std::is_void_v<V>)
    {
        return *m_value;
    }

    // 满足标准库迭代器的语义
    auto operator*() const {
        if constexpr (std::is_void_v<V>) {
            return m_key;
        } else {
            return std::pair<const K &, util::types::monostate_if_void<V> &>{m_key, *m_value};
        }
    }
};

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
auto &get(pair_ref<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return p.value();
    } else {
        return;
    }
}

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
auto &get(const pair_ref<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return const_cast<const decltype(p.value())>(p.value());
    } else {
        return;
    }
}

template<util::types::hash_key K, util::types::move_secure V>
struct pair_ref_const {
private:
    const K &m_key;
    const V *m_value;

public:
    pair_ref_const(const K &key)
        requires(std::is_void_v<V>)
            : m_key{key}
            , m_value{nullptr} {}

    pair_ref_const(const K &key, const util::types::monostate_if_void<V> &value)
        requires(!std::is_void_v<V>)
            : m_key{key}
            , m_value{&value} {}

    const K &key() const { return m_key; }

    const util::types::monostate_if_void<V> &value() const
        requires(!std::is_void_v<V>)
    {
        return *m_value;
    }

    // 满足标准库迭代器的语义
    auto operator*() const {
        if constexpr (std::is_void_v<V>) {
            return m_key;
        } else {
            return std::pair<const K &, const util::types::monostate_if_void<V> &>{m_key, *m_value};
        }
    }
};

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
auto &get(const pair_ref_const<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return p.value();
    } else {
        return;
    }
}

template<util::types::hash_key K, util::types::move_secure V>
class hash_map final {
    static constexpr std::size_t initial_capacity = 64;
    static constexpr double load_factor = 0.6;
    static constexpr std::size_t walkaround_step = 64;
    static constexpr std::size_t rehash_section_length = 64;

    using bucket_state = detail::bucket_state;

    struct bucket_v {
        util::raw_storage<V> value;
    };

    struct bucket : public std::conditional_t<std::is_void_v<V>, std::monostate, bucket_v> {
        std::atomic<bucket_state> state{bucket_state::empty};
        std::atomic_size_t guard_count{0};
        util::raw_storage<K> key;

        ~bucket() {
            if (state.load(std::memory_order::acquire) != bucket_state::filled) {
                return;
            }

            key.get()->~K();
            if constexpr (!std::is_void_v<V>) {
                bucket_v::value.get()->~V();
            }
        }
    };

public:
    // 此类只会在 V 是非 void 类型时被使用，不需要再考虑 void 类型了
    class guard {
        friend class hash_map;

    public:
        guard() = default;

        ~guard() {
            if (m_map) {
                if (m_bucket) {
                    if (1 == m_bucket->guard_count.fetch_sub(1, std::memory_order::acq_rel)) {
                        m_bucket->state.store(bucket_state::filled, std::memory_order::release);
                    }
                }
                m_map->undo_move_occupy();
            }
        }

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs) noexcept
                : m_map{rhs.m_map}
                , m_bucket{rhs.m_bucket} {
            rhs.m_map = nullptr;
            rhs.m_bucket = nullptr;
        }
        guard &operator=(guard &&rhs) noexcept {
            m_map = rhs.m_map;
            m_bucket = rhs.m_bucket;
            rhs.m_map = nullptr;
            rhs.m_bucket = nullptr;
            return *this;
        }

        operator bool() const { return m_map && m_bucket; }

        util::types::monostate_if_void<V> &operator*() {
            asco_assert_lint(*this, "hash_map::guard: guard 为空，无法解引用");
            return *m_bucket->value.get();
        }

        const util::types::monostate_if_void<V> &operator*() const {
            asco_assert_lint(*this, "hash_map::guard: guard 为空，无法解引用");
            return *m_bucket->value.get();
        }

        V *operator->() {
            asco_assert_lint(*this, "hash_map::guard: guard 为空，无法解引用");
            return m_bucket->value.get();
        }

        const V *operator->() const {
            asco_assert_lint(*this, "hash_map::guard: guard 为空，无法解引用");
            return m_bucket->value.get();
        }

    private:
        guard(hash_map *m, bucket *b)
                : m_map{m}
                , m_bucket{b} {}

        hash_map *m_map{nullptr};
        bucket *m_bucket{nullptr};
    };

    using pair_ref = pair_ref<K, V>;
    using pair_ref_const = pair_ref_const<K, V>;

    class iterator {
        friend class hash_map;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = pair_ref;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type;
        using reference = value_type;

        iterator() = default;

        ~iterator() noexcept {
            if (!m_map) {
                return;
            }

            if (m_guard) {
                m_guard.reset();
            }

            m_map->undo_move_occupy();
        }

        iterator(const iterator &rhs)
                : m_map{rhs.m_map}
                , m_index{rhs.m_index}
                , m_generation{rhs.m_generation} {
            if (!m_map) {
                return;
            }

            m_map->do_move_occupy();

            if (guard g{get_this_bucket()}) {
                m_guard = std::move(g);
            }
        }

        iterator &operator=(const iterator &rhs) {
            if (this == &rhs) {
                return *this;
            }

            this->~iterator();
            new (this) iterator{rhs};
            return *this;
        }

        reference operator*() const {
            generation_check();

            auto &b = m_map->m_buckets[m_index];
            if constexpr (std::is_void_v<V>) {
                return {*b.key.get()};
            } else {
                return {*b.key.get(), *b.value.get()};
            }
        }

        iterator &operator++() {
            generation_check();

            bool first_loop = true;

            while (true) {
                if (first_loop) {
                    m_guard.reset();
                    first_loop = false;
                }
                if (m_index == m_map->m_buckets.size()) {
                    break;
                }
                ++m_index;
                generation_check();
                if (m_index == m_map->m_buckets.size()) {
                    return *this;
                }
                if (guard g{get_this_bucket()}) {
                    m_guard = std::move(g);
                    break;
                }
            }

            return *this;
        }

        iterator operator++(int) {
            iterator res = *this;
            ++*this;
            return res;
        }

        bool operator==(const iterator &rhs) const {
            return m_map == rhs.m_map && m_index == rhs.m_index && m_generation == rhs.m_generation;
        }

    private:
        iterator(hash_map *map, std::size_t index, std::size_t generation)
                : m_map{map}
                , m_index{index}
                , m_generation{generation} {
            if (!m_map->do_move_occupy()) {
                panic("hash_map: 迭代器无法访问哈希表，因为哈希表正在被移动");
            }

            while (m_index < m_map->m_buckets.size()) {
                if (guard g{get_this_bucket()}) {
                    m_guard = std::move(g);
                    break;
                }
                ++m_index;
                generation_check();
            }
        }

        guard get_this_bucket() {
            if (!m_map->do_move_occupy()) {
                panic("hash_map: 迭代器无法访问哈希表，因为哈希表正在被移动");
            }

            if (m_index >= m_map->m_buckets.size()) {
                m_map->undo_move_occupy();
                return guard{};
            }

            if (auto s = bucket_state::filled; m_map->m_buckets[m_index].state.compare_exchange_strong(
                    s, bucket_state::guard_protecting, std::memory_order::acq_rel,
                    std::memory_order::relaxed)) {
                m_map->m_buckets[m_index].guard_count.fetch_add(1, std::memory_order::acq_rel);
                return guard{m_map, &m_map->m_buckets[m_index]};
            } else if (s == bucket_state::guard_protecting) {
                m_map->m_buckets[m_index].guard_count.fetch_add(1, std::memory_order::acq_rel);
                return guard{m_map, &m_map->m_buckets[m_index]};
            } else {
                m_map->undo_move_occupy();
                return guard{};
            }
        }

        void generation_check() const {
            if (!m_map) {
                panic("hash_map: 访问空迭代器");
            }

            if (m_generation != m_map->m_generation.load(std::memory_order::acquire)) {
                panic("hash_map: 迭代器失效");
            }
        }

        hash_map *m_map;
        std::size_t m_index;
        std::size_t m_generation;

        std::optional<guard> m_guard;
    };

    static_assert(std::forward_iterator<iterator>);

    class iterator_const {
        friend class hash_map;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = pair_ref_const;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type;
        using reference = value_type;

        iterator_const() = default;

        ~iterator_const() noexcept {
            if (!m_map) {
                return;
            }

            if (m_guard) {
                m_guard.reset();
            }

            m_map->undo_move_occupy();
        }

        iterator_const(const iterator_const &rhs)
                : m_map{rhs.m_map}
                , m_index{rhs.m_index}
                , m_generation{rhs.m_generation} {
            if (!m_map) {
                return;
            }

            m_map->do_move_occupy();

            if (guard g{get_this_bucket()}) {
                m_guard = std::move(g);
            }
        }

        iterator_const &operator=(const iterator_const &rhs) {
            if (this == &rhs) {
                return *this;
            }

            this->~iterator_const();
            new (this) iterator_const{rhs};
            return *this;
        }

        reference operator*() const {
            generation_check();

            auto &b = m_map->m_buckets[m_index];
            if constexpr (std::is_void_v<V>) {
                return {*b.key.get()};
            } else {
                return {*b.key.get(), *b.value.get()};
            }
        }

        iterator_const &operator++() {
            generation_check();

            bool first_loop = true;

            while (true) {
                if (first_loop) {
                    m_guard.reset();
                    first_loop = false;
                }
                if (m_index == m_map->m_buckets.size()) {
                    break;
                }
                ++m_index;
                generation_check();
                if (m_index == m_map->m_buckets.size()) {
                    return *this;
                }
                if (guard g{get_this_bucket()}) {
                    m_guard = std::move(g);
                    break;
                }
            }

            return *this;
        }

        iterator_const operator++(int) {
            generation_check();
            iterator_const res = *this;
            ++*this;
            return res;
        }

        bool operator==(const iterator_const &rhs) const {
            return m_map == rhs.m_map && m_index == rhs.m_index && m_generation == rhs.m_generation;
        }

    private:
        iterator_const(hash_map *map, std::size_t index, std::size_t generation)
                : m_map{map}
                , m_index{index}
                , m_generation{generation} {
            if (!m_map->do_move_occupy()) {
                panic("hash_map: 迭代器无法访问哈希表，因为哈希表正在被移动");
            }

            generation_check();
            while (m_index < m_map->m_buckets.size()) {
                if (guard g{get_this_bucket()}) {
                    m_guard = std::move(g);
                    break;
                }
                ++m_index;
                generation_check();
            }
        }

        guard get_this_bucket() {
            if (!m_map->do_move_occupy()) {
                panic("hash_map: 迭代器无法访问哈希表，因为哈希表正在被移动");
            }

            if (m_index >= m_map->m_buckets.size()) {
                m_map->undo_move_occupy();
                return guard{};
            }

            if (auto s = bucket_state::filled; m_map->m_buckets[m_index].state.compare_exchange_strong(
                    s, bucket_state::guard_protecting, std::memory_order::acq_rel,
                    std::memory_order::relaxed)) {
                m_map->m_buckets[m_index].guard_count.fetch_add(1, std::memory_order::acq_rel);
                return guard{m_map, &m_map->m_buckets[m_index]};
            } else if (s == bucket_state::guard_protecting) {
                m_map->m_buckets[m_index].guard_count.fetch_add(1, std::memory_order::acq_rel);
                return guard{m_map, &m_map->m_buckets[m_index]};
            } else {
                m_map->undo_move_occupy();
                return guard{};
            }
        }

        void generation_check() const {
            if (!m_map) {
                panic("hash_map: 访问空迭代器");
            }

            if (m_generation != m_map->m_generation.load(std::memory_order::acquire)) {
                panic("hash_map: 迭代器失效");
            }
        }

        hash_map *m_map;
        std::size_t m_index;
        std::size_t m_generation;

        std::optional<guard> m_guard;
    };

    static_assert(std::forward_iterator<iterator_const>);

    explicit hash_map(std::size_t capacity = initial_capacity)
            : m_buckets(std::max((capacity + 7) / 8 * 8, initial_capacity)) {}

    ~hash_map() { do_move_lock(); }

    hash_map(const hash_map &) = delete;
    hash_map &operator=(const hash_map &) = delete;

    hash_map(hash_map &&rhs) {
        if (!rhs.do_move_lock()) {
            panic("hash_map: 从已移动的 hash_map 移动");
        }

        m_hasher = std::move(rhs.m_hasher);
        m_buckets = std::move(rhs.m_buckets);
        m_rehashing = rhs.m_rehashing.load(std::memory_order::acquire);
        m_pairs_count = rhs.m_pairs_count.load(std::memory_order::acquire);
        m_generation = rhs.m_generation.load(std::memory_order::acquire);
    }

    hash_map &operator=(hash_map &&rhs) {
        if (this == &rhs) {
            return *this;
        }

        if (!rhs.do_move_lock()) {
            panic("hash_map: 从已移动的 hash_map 移动");
        }

        m_hasher = std::move(rhs.m_hasher);
        m_buckets = std::move(rhs.m_buckets);
        m_rehashing = rhs.m_rehashing.load(std::memory_order::acquire);
        m_pairs_count = rhs.m_pairs_count.load(std::memory_order::acquire);
        m_generation = rhs.m_generation.load(std::memory_order::acquire);

        return *this;
    }

    iterator begin() { return iterator{this, 0, m_generation.load(std::memory_order::acquire)}; }
    iterator end() { return iterator{this, m_buckets.size(), m_generation.load(std::memory_order::acquire)}; }

    iterator_const begin() const {
        return iterator_const{const_cast<hash_map *>(this), 0, m_generation.load(std::memory_order::acquire)};
    }
    iterator_const end() const {
        return iterator_const{
            const_cast<hash_map *>(this), m_buckets.size(), m_generation.load(std::memory_order::acquire)};
    }

    // 保证在无法插入时反复 rehash 直到插入成功
    //
    // 返回值为 std::nullopt 表示插入成功，返回值为 std::move(value) 表示插入失败
    std::expected<std::monostate, util::types::monostate_if_void<V>>
    insert(const K &key, util::types::monostate_if_void<V> value)
        requires(!std::is_void_v<V>)
    {
        if (!do_move_occupy()) {
            return std::unexpected{std::move(value)};
        }

        if (is_rehashing()) {
            do_rehash_until_success();
        }

        step_generation();

        std::optional<std::size_t> index;

        while (!(index = do_insert(m_buckets, key, std::move(value)))) {
            rehash();
        }

        if (must_rehash()) {
            rehash();
        }

        undo_move_occupy();

        return std::monostate{};
    }

    // 保证在无法插入时反复 rehash 直到插入成功
    //
    // 返回值为 std::nullopt 表示插入成功，返回值为 std::move(value) 表示插入失败
    bool insert(const K &key)
        requires(std::is_void_v<V>)
    {
        if (!do_move_occupy()) {
            return false;
        }

        if (is_rehashing()) {
            do_rehash_until_success();
        }

        step_generation();

        std::optional<std::size_t> index;

        while (!(index = do_insert(m_buckets, key, std::monostate{}))) {
            rehash();
        }

        if (must_rehash()) {
            rehash();
        }

        undo_move_occupy();

        return true;
    }

    std::optional<util::types::monostate_if_void<V>> remove(const K &key)
        requires(!std::is_void_v<V>)
    {
        if (!do_move_occupy()) {
            return std::nullopt;
        }

        if (is_rehashing()) {
            do_rehash_until_success();
        }

        step_generation();

        auto res = do_remove(m_buckets, key);

        undo_move_occupy();

        return res;
    }

    bool remove(const K &key)
        requires(std::is_void_v<V>)
    {
        if (!do_move_occupy()) {
            return false;
        }

        if (is_rehashing()) {
            do_rehash_until_success();
        }

        step_generation();

        auto res = do_remove(m_buckets, key);

        undo_move_occupy();

        return res;
    }

    // 守卫会阻塞插入、移除等操作，应在完成操作后马上释放守卫避免死锁
    guard get(const K &key)
        requires(!std::is_void_v<V>)
    {
        if (!do_move_occupy()) {
            return {nullptr, nullptr};
        }

        if (is_rehashing()) {
            do_rehash_until_success();
        }

        return {this, do_get(key)};
    }

    bool contains(const K &key) const { return const_cast<hash_map *>(this)->contains(key); }

    bool contains(const K &key) {
        if (!do_move_occupy()) {
            return false;
        }

        if (is_rehashing()) {
            do_rehash_until_success();
        }

        return guard{this, do_get(key)};
    }

    void rehash() {
        if (!do_move_occupy()) {
            return;
        }

        if (m_rehashing.exchange(true, std::memory_order::acq_rel)) {
            while (!m_can_help_rehashing.load(std::memory_order::acquire)) {
                cpu_relax();
            }
            do_rehash_until_success();

            undo_move_occupy();
            return;
        }

        step_generation();

        m_rehash_progress.store(0, std::memory_order::release);
        m_rehash_buckets = std::vector<bucket>(m_buckets.size() * 2);
        m_rehash_buckets.swap(m_buckets);
        m_pairs_count.store(0, std::memory_order::release);

        while (true) {
            m_can_help_rehashing.store(true, std::memory_order::release);
            if (do_rehash(false)) {
                m_rehashing.store(false, std::memory_order::release);
                m_can_help_rehashing.store(false, std::memory_order::release);
                break;
            } else {
                m_can_help_rehashing.store(false, std::memory_order::release);
                step_generation();
                m_rehash_progress.store(0, std::memory_order::release);
                m_can_help_rehashing.store(true, std::memory_order::release);
                do_rehash(true);  // 原来的一定不会插入失败，所以直接忽略返回值
                m_can_help_rehashing.store(false, std::memory_order::release);
                m_buckets = std::vector<bucket>(m_buckets.size() * 2);
            }
        }

        while (m_help_rehash_thread_count.load(std::memory_order::acquire) != 0) {
            cpu_relax();
        }

        m_rehash_buckets.clear();

        undo_move_occupy();
    }

private:
    // fetch_add 语义
    std::size_t step_generation() { return m_generation.fetch_add(1, std::memory_order::acq_rel); }

    // 返回值表示是否真的锁了，已经锁过返回 false
    bool do_move_lock() {
        for (  //
            auto s = move_state::accesible;
            !m_move_state.compare_exchange_weak(s, move_state::moved, std::memory_order::acq_rel);) {
            if (s == move_state::moved) {
                return false;
            }
            if (s == move_state::occupied) {
                panic("hash_map: 不允许在正在被占用的 hash_map 上进行移动操作");
            }
            s = move_state::accesible;
        }
        return true;
    }

    // 仅用于避免被移动，其它操作之间不会互斥。返回 false 表示当前哈希表正在/已经被移动
    bool do_move_occupy() {
        if (  //
            auto s = move_state::accesible; !m_move_state.compare_exchange_strong(
                s, move_state::occupied, std::memory_order::acq_rel, std::memory_order::relaxed)) {
            if (s == move_state::moved) {
                return false;
            }
        }

        m_occupy_count.fetch_add(1, std::memory_order::acq_rel);

        return true;
    }

    // 仅用于避免被移动，其它操作之间不会互斥
    void undo_move_occupy() {
        if (m_occupy_count.fetch_sub(1, std::memory_order::acq_rel) == 1) {
            m_move_state.store(move_state::accesible, std::memory_order::release);
        }
    }

    bool must_rehash() {
        auto pairs_count = m_pairs_count.load(std::memory_order::acquire);
        auto buckets_count = m_buckets.size();
        return pairs_count > static_cast<std::size_t>(buckets_count * load_factor);
    }

    bool must_help_rehash() { return m_can_help_rehashing.load(std::memory_order::acquire); }

    bool is_rehashing() { return m_rehashing.load(std::memory_order::acquire); }

    // 返回插入是否成功，失败时保证 value 完全没有被移动
    std::optional<std::size_t>
    do_insert(std::vector<bucket> &buckets, const K &key, util::types::monostate_if_void<V> &&value) {
        std::size_t size = buckets.size();
        std::size_t hash = m_hasher(key) % size;
        for (std::size_t i = 0; i < walkaround_step; i++) {
        this_insert_start:
            auto &b = buckets[(hash + i) % size];
            auto s = b.state.load(std::memory_order::acquire);

            if (s == bucket_state::empty) {
                if (!b.state.compare_exchange_strong(
                        s, bucket_state::constructing, std::memory_order::acq_rel,
                        std::memory_order::relaxed)) {
                    continue;
                }
            } else if (
                s == bucket_state::constructing || s == bucket_state::filled
                || s == bucket_state::guard_protecting) {
                do {
                    s = b.state.load(std::memory_order::acquire);
                    cpu_relax();
                } while (s == bucket_state::constructing || s == bucket_state::guard_protecting);

                for (  //
                    s = bucket_state::filled; !b.state.compare_exchange_weak(
                        s, bucket_state::guard_protecting, std::memory_order::acq_rel,
                        std::memory_order::relaxed);
                    s = bucket_state::filled) {
                    if (s != bucket_state::constructing) {
                        goto this_insert_start;
                    }
                    cpu_relax();
                }
                b.guard_count.fetch_add(1, std::memory_order::acq_rel);

                if (*b.key.get() != key) {
                    if (1 == b.guard_count.fetch_sub(1, std::memory_order::acq_rel)) {
                        b.state.store(bucket_state::filled, std::memory_order::release);
                    }
                    continue;
                } else {
                    b.state.store(bucket_state::constructing, std::memory_order::release);
                    while (b.guard_count.load(std::memory_order::acquire) > 1) {
                        cpu_relax();
                    }

                    try {
                        b.key.get()->~K();
                        if constexpr (!std::is_void_v<V>) {
                            b.value.get()->~V();
                        }
                    } catch (...) {
                        if (1 == b.guard_count.fetch_sub(1, std::memory_order::acq_rel)) {
                            b.state.store(bucket_state::filled, std::memory_order::release);
                        }
                        std::rethrow_exception(std::current_exception());
                    }
                    m_pairs_count.fetch_sub(1, std::memory_order::acq_rel);

                    b.guard_count.fetch_sub(1, std::memory_order::acq_rel);
                }
            } else if (s == bucket_state::destructing || s == bucket_state::tombstone) {
                for (                             //
                    s = bucket_state::tombstone;  //
                    !b.state.compare_exchange_weak(
                        s, bucket_state::constructing, std::memory_order::acq_rel,
                        std::memory_order::relaxed);  //
                    s = bucket_state::tombstone) {
                    if (s == bucket_state::constructing) {
                        continue;
                    }
                    cpu_relax();
                }
            }

            try {
                new (b.key.get()) K{key};
                if constexpr (!std::is_void_v<V>) {
                    new (b.value.get()) V{std::move(value)};
                }
            } catch (...) {
                b.state.store(bucket_state::filled, std::memory_order::release);
                std::rethrow_exception(std::current_exception());
            }

            b.state.store(bucket_state::filled, std::memory_order::release);
            m_pairs_count.fetch_add(1, std::memory_order::acq_rel);
            return (hash + i) % size;
        }
        return std::nullopt;
    }

    std::optional<util::types::monostate_if_void<V>> do_remove(std::vector<bucket> &buckets, const K &key) {
        std::size_t size = buckets.size();
        std::size_t hash = m_hasher(key) % size;
        for (std::size_t i = 0; i < walkaround_step; i++) {
            auto &b = buckets[(hash + i) % size];
            auto s = b.state.load(std::memory_order::acquire);

            if (s == bucket_state::empty) {
                return std::nullopt;
            }

            if (s == bucket_state::destructing || s == bucket_state::tombstone) {
                continue;
            }

            if (s == bucket_state::constructing || s == bucket_state::filled
                || s == bucket_state::guard_protecting) {
                do {
                    s = b.state.load(std::memory_order::acquire);
                    cpu_relax();
                } while (s == bucket_state::constructing || s == bucket_state::guard_protecting);

                for (  //
                    s = bucket_state::filled; !b.state.compare_exchange_weak(
                        s, bucket_state::destructing, std::memory_order::acq_rel, std::memory_order::relaxed);
                    s = bucket_state::filled) {
                    if (s == bucket_state::destructing || s == bucket_state::tombstone) {
                        return std::nullopt;
                    }
                    cpu_relax();
                }

                if (*b.key.get() != key) {
                    b.state.store(bucket_state::filled, std::memory_order::release);
                    continue;
                }
            }
            try {
                if constexpr (std::is_void_v<V>) {
                    b.key.get()->~K();
                    b.state.store(bucket_state::tombstone, std::memory_order::release);
                    m_pairs_count.fetch_sub(1, std::memory_order::acq_rel);
                    return std::monostate{};
                } else {
                    auto res = std::move(*b.value.get());
                    b.key.get()->~K();
                    b.value.get()->~V();
                    b.state.store(bucket_state::tombstone, std::memory_order::release);
                    m_pairs_count.fetch_sub(1, std::memory_order::acq_rel);
                    return res;
                }
            } catch (...) {
                b.state.store(bucket_state::filled, std::memory_order::release);
                std::rethrow_exception(std::current_exception());
            }
        }
        return std::nullopt;
    }

    bucket *do_get(const K &key) {
        std::size_t size = m_buckets.size();
        std::size_t hash = m_hasher(key) % size;
        for (std::size_t i = 0; i < walkaround_step; i++) {
            auto &b = m_buckets[(hash + i) % size];
            auto s = b.state.load(std::memory_order::acquire);

            if (s == bucket_state::empty) {
                return nullptr;
            }

            if (s == bucket_state::destructing || s == bucket_state::tombstone) {
                continue;
            }

            b.guard_count.fetch_add(1, std::memory_order::acq_rel);
            while (b.state.load(std::memory_order::acquire) == bucket_state::constructing) {
                cpu_relax();
            }

            if (s = bucket_state::filled; !b.state.compare_exchange_strong(
                    s, bucket_state::guard_protecting, std::memory_order::acq_rel,
                    std::memory_order::relaxed)) {
                if (s != bucket_state::guard_protecting) {
                    b.guard_count.fetch_sub(1, std::memory_order::acq_rel);
                    continue;
                }
            }
            if (*b.key.get() == key) {
                return &b;
            }
            if (1 == b.guard_count.fetch_sub(1, std::memory_order::acq_rel)) {
                b.state.store(bucket_state::filled, std::memory_order::release);
            }
        }
        return nullptr;
    }

    void do_rehash_until_success() {
        m_help_rehash_thread_count.fetch_add(1, std::memory_order::acq_rel);

        while (must_help_rehash()) {
            if (!do_rehash(false)) {
                while (!must_help_rehash()) {
                    cpu_relax();
                }
                do_rehash(true);
                while (!must_help_rehash()) {
                    cpu_relax();
                }
            }
        }

        m_help_rehash_thread_count.fetch_sub(1, std::memory_order::acq_rel);
    }

    // 随机顺序交错遍历 ，使得并发重哈希时每个线程都能有机会迁移元素
    //
    // 返回重哈希是否成功
    bool do_rehash(bool reverse) {
        auto gener = m_generation.load(std::memory_order::relaxed);

        auto &target_buckets = reverse ? m_rehash_buckets : m_buckets;
        auto &source_buckets = reverse ? m_buckets : m_rehash_buckets;

        for (  //
            std::size_t t = 0;
            (t = m_rehash_progress.fetch_add(rehash_section_length, std::memory_order::acq_rel))
            < source_buckets.size();) {
            auto end = t + rehash_section_length;
            for (std::size_t i = t; i < end && i < source_buckets.size(); i++) {
                auto &b = source_buckets[i];
                auto state = b.state.load(std::memory_order::acquire);

                for (  //
                    auto s = bucket_state::filled; !b.state.compare_exchange_weak(
                        s, bucket_state::destructing, std::memory_order::acq_rel, std::memory_order::relaxed);
                    s = bucket_state::filled) {
                    if (s != bucket_state::filled && s != bucket_state::guard_protecting) {
                        goto continue_outer;
                    }
                    cpu_relax();
                }
                if (false) {
                continue_outer:
                    continue;
                }

                auto &key = *b.key.get();
                try {
                    // 这里直接丢弃 do_insert 的返回值是正确的，
                    // 因为如果会插入失败，那么早在开始 rehash 之前的任意插入操作就该失败了
                    if constexpr (std::is_void_v<V>) {
                        auto res = do_insert(target_buckets, key, std::monostate{});
                        asco_assert_lint(res, "hash_map: 重哈希时意外的插入失败");
                    } else {
                        auto res = do_insert(target_buckets, key, std::move(*b.value.get()));
                        asco_assert_lint(res, "hash_map: 重哈希时意外的插入失败");
                        b.value.get()->~V();
                    }
                    b.key.get()->~K();
                } catch (...) {
                    b.state.store(bucket_state::filled, std::memory_order::release);
                    std::rethrow_exception(std::current_exception());
                }
                b.state.store(bucket_state::tombstone, std::memory_order::release);

                if (!must_help_rehash()) {
                    return !is_rehashing();
                } else if (m_generation.load(std::memory_order::acquire) != gener) {
                    return false;
                }
            }
        }
        return true;
    }

    std::hash<K> m_hasher;
    std::vector<bucket> m_buckets;
    std::atomic_size_t m_pairs_count{0};

    std::atomic_size_t m_generation{0};
    std::atomic_bool m_rehashing{false};
    std::atomic_bool m_can_help_rehashing{false};
    std::vector<bucket> m_rehash_buckets;
    std::atomic_size_t m_rehash_progress{0};
    std::atomic_size_t m_help_rehash_thread_count{0};

    enum class move_state { accesible, occupied, moved };
    std::atomic<move_state> m_move_state{move_state::accesible};
    std::atomic_size_t m_occupy_count{0};
};

template<util::types::hash_key K>
using hash_set = hash_map<K, void>;

};  // namespace asco::concurrency

namespace std {

template<asco::util::types::hash_key K, asco::util::types::move_secure V>
struct tuple_size<asco::concurrency::pair_ref<K, V>>
        : std::conditional_t<
              std::is_void_v<V>, std::integral_constant<std::size_t, 1>,
              std::integral_constant<std::size_t, 2>> {};

template<asco::util::types::hash_key K, asco::util::types::move_secure V>
struct tuple_size<asco::concurrency::pair_ref_const<K, V>>
        : std::conditional_t<
              std::is_void_v<V>, std::integral_constant<std::size_t, 1>,
              std::integral_constant<std::size_t, 2>> {};

template<asco::util::types::hash_key K, asco::util::types::move_secure V>
struct tuple_element<0, asco::concurrency::pair_ref<K, V>> {
    using type = const K &;
};

template<asco::util::types::hash_key K, asco::util::types::move_secure V>
    requires(!std::is_void_v<V>)
struct tuple_element<1, asco::concurrency::pair_ref<K, V>> {
    using type = asco::util::types::monostate_if_void<V> &;
};

template<asco::util::types::hash_key K, asco::util::types::move_secure V>
struct tuple_element<0, asco::concurrency::pair_ref_const<K, V>> {
    using type = const K &;
};

template<asco::util::types::hash_key K, asco::util::types::move_secure V>
    requires(!std::is_void_v<V>)
struct tuple_element<1, asco::concurrency::pair_ref_const<K, V>> {
    using type = const asco::util::types::monostate_if_void<V> &;
};

};  // namespace std
