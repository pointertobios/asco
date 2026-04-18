// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <asco/concurrency/concurrency.h>
#include <asco/panic.h>
#include <asco/sync/spinrwlock.h>
#include <asco/util/murmur.h>
#include <asco/util/raw_storage.h>
#include <asco/util/types.h>

// 并发哈希表（不保证值的并发安全）

namespace asco::concurrency {

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
};

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
};

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

namespace asco::concurrency {

namespace detail {

enum class bucket_state_enum : std::uint8_t {
    empty = 0,
    constructing,
    filled,
    predestructing,
    destructing,
    tombstone,
};

struct bucket_state {
    std::size_t refcount : 61;
    bucket_state_enum state : 3;

#ifdef _WIN32  // MSVC 的位域有填充，需要显式清零
    bucket_state() { std::memset(this, 0, sizeof(*this)); }

    bucket_state(std::size_t rc, bucket_state_enum s) {
        std::memset(this, 0, sizeof(*this));
        refcount = rc;
        state = s;
    }
#endif
};

};  // namespace detail

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
std::tuple_element<I, pair_ref<K, V>>::type get(pair_ref<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return p.value();
    } else {
        std::unreachable();
    }
}

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
std::tuple_element<I, pair_ref<K, V>>::type get(pair_ref<K, V> &&p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return p.value();
    } else {
        std::unreachable();
    }
}

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
std::tuple_element<I, pair_ref_const<K, V>>::type get(const pair_ref_const<K, V> &p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return p.value();
    } else {
        std::unreachable();
    }
}

template<std::size_t I, util::types::hash_key K, util::types::move_secure V>
std::tuple_element<I, pair_ref_const<K, V>>::type get(const pair_ref_const<K, V> &&p) {
    if constexpr (I == 0) {
        return p.key();
    } else if constexpr (I == 1 && !std::is_void_v<V>) {
        return p.value();
    } else {
        std::unreachable();
    }
}

enum class insert_failed {
    rehashing,
    key_repeated,
    rehash_needed,
    retry,
};

enum class remove_failed {
    none,
    retry,
    rehashing,
    guard_protecting,
};

enum class get_failed {
    none,
    rehashing,
    retry,
};

using contains_failed = get_failed;

template<util::types::hash_key K, util::types::move_secure V>
    requires(
        (std::is_nothrow_move_constructible_v<V> && std::is_nothrow_destructible_v<V>) || std::is_void_v<V>)
class hash_map final {
    static constexpr std::size_t initial_capacity = 64;
    static constexpr double load_factor = 0.6;

    using bucket_state_enum = detail::bucket_state_enum;
    using bucket_state = detail::bucket_state;

    struct bucket {
        std::atomic<bucket_state> state{bucket_state{0, bucket_state_enum::empty}};

        util::raw_storage<K> key;
        [[no_unique_address]] util::raw_storage<util::types::monostate_if_void<V>> value;
    };

public:
    class guard {
        friend class hash_map;

    public:
        guard() = default;

        ~guard() {
            if (!m_bucket) {
                return;
            }

            bucket_state e;
            do {
                e = m_bucket->state.load(std::memory_order::acquire);
            } while (!m_bucket->state.compare_exchange_weak(
                e, {e.refcount - 1, e.state}, std::memory_order::acq_rel, std::memory_order::relaxed));
        }

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs) noexcept
                : m_bucket{rhs.m_bucket} {
            rhs.m_bucket = nullptr;
        }

        guard &operator=(guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~guard();
                new (this) guard(std::move(rhs));
            }
            return *this;
        }

        operator bool() const noexcept { return m_bucket != nullptr; }

        pair_ref<K, util::types::monostate_if_void<V>> operator*() {
            asco_assert(m_bucket != nullptr);
            return pair_ref<K, util::types::monostate_if_void<V>>{
                *m_bucket->key.get(), *m_bucket->value.get()};
        }

        pair_ref_const<K, util::types::monostate_if_void<V>> operator*() const {
            asco_assert(m_bucket != nullptr);
            return pair_ref_const<K, util::types::monostate_if_void<V>>{
                *m_bucket->key.get(), *m_bucket->value.get()};
        }

        const K &key() const {
            asco_assert(m_bucket != nullptr);
            return *m_bucket->key.get();
        }

        util::types::monostate_if_void<V> &value()
            requires(!std::is_void_v<V>)
        {
            asco_assert(m_bucket != nullptr);
            return *m_bucket->value.get();
        }

        const util::types::monostate_if_void<V> &value() const
            requires(!std::is_void_v<V>)
        {
            asco_assert(m_bucket != nullptr);
            return *m_bucket->value.get();
        }

    private:
        guard(bucket *b) noexcept
                : m_bucket{b} {}

        bucket *m_bucket{nullptr};
    };

    hash_map()
            : m_buckets(initial_capacity) {}

    ~hash_map()
#ifdef ASCO_TESTING
        noexcept(false)
#endif
    {
        auto g = m_lock.try_write();
        if (!g) {
            panic("asco::concurrency::hash_map: 析构 hash_map 时正在被结构保护或 rehash");
        }

        for (bucket &b : m_buckets) {
            bucket_state e = b.state.load(std::memory_order::acquire);
            if (e.state == bucket_state_enum::filled) {
                while ((e = b.state.load(std::memory_order::acquire)).refcount) {
                    concurrency::cpu_relax();
                }
                b.key.get()->~K();
                if constexpr (!std::is_void_v<V>) {
                    b.value.get()->~V();
                }
            }
        }
    }

    hash_map(const hash_map &) = delete;
    hash_map &operator=(const hash_map &) = delete;

    hash_map(hash_map &&) = delete;
    hash_map &operator=(hash_map &&) = delete;

    std::size_t size() const { return m_load.load(std::memory_order::relaxed); }

    // 保证在失败时 `value` 参数没有被移动（不抛异常时）
    std::expected<std::monostate, insert_failed>
    try_insert(const K &key, util::types::monostate_if_void<V> &&value) {
        auto g = m_lock.try_read();
        if (!g) {
            return std::unexpected(insert_failed::rehashing);
        }

        return do_insert(key, std::move(value));
    }

    auto try_insert(const K &key)
        requires(std::is_void_v<V>)
    {
        return try_insert(key, {});
    }

    bool insert(const K &key, util::types::copy_small_or_move<util::types::monostate_if_void<V>> value) {
        while (true) {
            auto res = try_insert(key, std::move(value));
            if (res) {
                return true;
            }

            switch (res.error()) {
            case insert_failed::rehashing:
                concurrency::cpu_relax();
                break;
            case insert_failed::key_repeated:
                return false;
            case insert_failed::rehash_needed:
                try_rehash();
                break;
            case insert_failed::retry:
                concurrency::cpu_relax();
                continue;
            }
        }
    }

    bool insert(const K &key)
        requires(std::is_void_v<V>)
    {
        return insert(key, {});
    }

    std::expected<util::types::monostate_if_void<V>, remove_failed> try_remove(const K &key) {
        auto sg = m_lock.try_read();
        if (!sg) {
            return std::unexpected(remove_failed::rehashing);
        }

        return do_remove(key);
    }

    std::optional<util::types::monostate_if_void<V>> remove(const K &key)
        requires(!std::is_void_v<V>)
    {
        while (true) {
            auto res = try_remove(key);
            if (res) {
                return std::move(res.value());
            }

            switch (res.error()) {
            case remove_failed::none:
                return std::nullopt;
            case remove_failed::rehashing:
                concurrency::cpu_relax();
                break;
            case remove_failed::guard_protecting:
            case remove_failed::retry:
                concurrency::cpu_relax();
                continue;
            }
        }
    }

    bool remove(const K &key)
        requires(std::is_void_v<V>)
    {
        while (true) {
            auto res = try_remove(key);
            if (res) {
                return true;
            }

            switch (res.error()) {
            case remove_failed::none:
                return false;
            case remove_failed::rehashing:
                concurrency::cpu_relax();
                break;
            case remove_failed::guard_protecting:
            case remove_failed::retry:
                concurrency::cpu_relax();
                continue;
            }
        }
    }

    // 不应长时间持有 guard，应尽快完成操作并释放 guard
    std::expected<guard, get_failed> try_get(const K &key)
        requires(!std::is_void_v<V>)
    {
        auto sg = m_lock.try_read();
        if (!sg) {
            return std::unexpected(get_failed::rehashing);
        }

        return do_get(key);
    }

    std::expected<std::monostate, get_failed> try_get(const K &key)
        requires(std::is_void_v<V>)
    {
        auto sg = m_lock.try_read();
        if (!sg) {
            return std::unexpected(get_failed::rehashing);
        }

        if (auto g = do_get(key)) {
            return std::monostate{};
        } else {
            return std::unexpected{g.error()};
        }
    }

    guard get(const K &key)
        requires(!std::is_void_v<V>)
    {
        while (true) {
            auto res = try_get(key);
            if (res) {
                return std::move(res.value());
            }

            switch (res.error()) {
            case get_failed::none:
                return guard{};
            case get_failed::rehashing:
                concurrency::cpu_relax();
                break;
            case get_failed::retry:
                concurrency::cpu_relax();
                continue;
            }
        }
    }

    std::expected<bool, contains_failed> try_contains(const K &key) {
        auto sg = m_lock.try_read();
        if (!sg) {
            return std::unexpected{contains_failed::rehashing};
        }

        if (auto g = do_get(key)) {
            return true;
        } else if (g.error() == contains_failed::none) {
            return false;
        } else {
            return std::unexpected{g.error()};
        }
    }

    bool contains(const K &key) {
        while (true) {
            auto res = try_contains(key);
            if (res) {
                return res.value();
            }

            switch (res.error()) {
            case contains_failed::rehashing:
                concurrency::cpu_relax();
                break;
            case contains_failed::retry:
                concurrency::cpu_relax();
                continue;
            }
        }
    }

    // 不保证一定完成 rehash ，没有完成 rehash 时返回 false
    bool try_rehash() {
        auto rg = m_lock.try_write();
        if (!rg) {
            return false;
        }

        do_rehash();

        return true;
    }

private:
    std::expected<std::monostate, insert_failed>
    do_insert(const K &key, util::types::monostate_if_void<V> &&value) {
        if (static_cast<double>(m_load.load(std::memory_order::acquire)) / m_buckets.size() >= load_factor) {
            return std::unexpected(insert_failed::rehash_needed);
        }

        auto size = m_buckets.size();
        auto hash = hash1(key);
        auto begindex = hash % size;
        auto step = hash2(key);

        std::optional<std::size_t> insert_index{std::nullopt};

        for (std::size_t i = 0; i < size; i++) {
            auto index = (begindex + i * (step % size)) % size;
            bucket &b = m_buckets[index];

        begin_insert:
            if (insert_index) {
                bucket_state e = b.state.load(std::memory_order::acquire);
                if (e.state == bucket_state_enum::tombstone) {
                    continue;
                } else if (e.state == bucket_state_enum::empty) {
                    break;
                } else if (
                    e.state == bucket_state_enum::constructing || e.state == bucket_state_enum::predestructing
                    || e.state == bucket_state_enum::destructing) {
                    m_buckets[*insert_index].state.store(
                        {0, bucket_state_enum::tombstone}, std::memory_order::release);
                    return std::unexpected{insert_failed::retry};
                } else if (e.state == bucket_state_enum::filled) {
                    if (b.state.compare_exchange_strong(
                            e, {e.refcount + 1, e.state}, std::memory_order::acq_rel,
                            std::memory_order::relaxed)) {
                        if (*b.key.get() == key) {
                            do {
                                e = b.state.load(std::memory_order::acquire);
                            } while (!b.state.compare_exchange_weak(
                                e, {e.refcount - 1, e.state}, std::memory_order::acq_rel,
                                std::memory_order::relaxed));
                            m_buckets[*insert_index].state.store(
                                {0, bucket_state_enum::tombstone}, std::memory_order::release);
                            return std::unexpected{insert_failed::key_repeated};
                        } else {
                            do {
                                e = b.state.load(std::memory_order::acquire);
                            } while (!b.state.compare_exchange_weak(
                                e, {e.refcount - 1, e.state}, std::memory_order::acq_rel,
                                std::memory_order::relaxed));
                            continue;
                        }
                    } else if (e.state == bucket_state_enum::predestructing) {
                        m_buckets[*insert_index].state.store(
                            {0, bucket_state_enum::tombstone}, std::memory_order::release);
                        return std::unexpected{insert_failed::retry};
                    } else {
                        goto begin_insert;
                    }
                }

                std::unreachable();
            }

            if (bucket_state e{0, bucket_state_enum::empty};  //
                !b.state.compare_exchange_strong(
                    e, {0, bucket_state_enum::constructing}, std::memory_order::acq_rel,
                    std::memory_order::relaxed)) {
                if (e.state == bucket_state_enum::constructing
                    || e.state == bucket_state_enum::predestructing) {
                    return std::unexpected{insert_failed::retry};
                } else if (e.state == bucket_state_enum::filled) {
                    if (b.state.compare_exchange_strong(
                            e, {e.refcount + 1, e.state}, std::memory_order::acq_rel,
                            std::memory_order::relaxed)) {
                        if (*b.key.get() == key) {
                            do {
                                e = b.state.load(std::memory_order::acquire);
                            } while (!b.state.compare_exchange_weak(
                                e, {e.refcount - 1, e.state}, std::memory_order::acq_rel,
                                std::memory_order::relaxed));
                            return std::unexpected{insert_failed::key_repeated};
                        } else {
                            do {
                                e = b.state.load(std::memory_order::acquire);
                            } while (!b.state.compare_exchange_weak(
                                e, {e.refcount - 1, e.state}, std::memory_order::acq_rel,
                                std::memory_order::relaxed));
                            continue;
                        }
                    } else if (e.state == bucket_state_enum::predestructing) {
                        return std::unexpected{insert_failed::retry};
                    } else {
                        goto begin_insert;
                    }
                } else if (e.state == bucket_state_enum::destructing) {
                    return std::unexpected{insert_failed::retry};
                } else if (e.state == bucket_state_enum::tombstone) {
                    do {
                        e = b.state.load(std::memory_order::acquire);
                        if (e.state != bucket_state_enum::tombstone) {
                            goto begin_insert;
                        }
                    } while (!b.state.compare_exchange_weak(
                        e, {0, bucket_state_enum::constructing}, std::memory_order::acq_rel,
                        std::memory_order::relaxed));
                    if (!insert_index) {
                        insert_index = index;
                    }
                    continue;
                } else if (e.state == bucket_state_enum::empty) {
                    goto begin_insert;
                } else {
                    continue;
                }
            }

            insert_index = index;

            break;
        }

        if (insert_index) {
            bucket &b = m_buckets[*insert_index];
            new (b.key.get()) K(key);
            if constexpr (!std::is_void_v<V>) {
                new (b.value.get()) util::types::monostate_if_void<V>(std::move(value));
            }
            m_load.fetch_add(1, std::memory_order::relaxed);
            b.state.store({0, bucket_state_enum::filled}, std::memory_order::release);

            return {};
        } else {
            std::unreachable();
        }
    }

    std::expected<util::types::monostate_if_void<V>, remove_failed> do_remove(const K &key) {
        auto size = m_buckets.size();
        auto hash = hash1(key);
        auto begindex = hash % size;
        auto step = hash2(key);

        for (std::size_t i = 0; i < size; i++) {
            auto index = (begindex + i * (step % size)) % size;
            bucket &b = m_buckets[index];

        begin_remove:
            if (bucket_state e{0, bucket_state_enum::filled};  //
                !b.state.compare_exchange_strong(
                    e, {1, bucket_state_enum::predestructing}, std::memory_order::acq_rel,
                    std::memory_order::relaxed)) {
                if (e.state == bucket_state_enum::filled && e.refcount != 0) {
                    do {
                        e = b.state.load(std::memory_order::acquire);
                        if (e.state != bucket_state_enum::filled) {
                            return std::unexpected{remove_failed::retry};
                        } else if (e.refcount == 0) {
                            goto begin_remove;
                        }
                    } while (!b.state.compare_exchange_weak(
                        e, {e.refcount + 1, bucket_state_enum::predestructing}, std::memory_order::acq_rel,
                        std::memory_order::relaxed));
                } else if (e.state == bucket_state_enum::empty) {
                    return std::unexpected{remove_failed::none};
                } else if (
                    e.state == bucket_state_enum::constructing
                    || e.state == bucket_state_enum::predestructing) {
                    return std::unexpected{remove_failed::retry};
                } else {
                    continue;
                }
            }

            if (*b.key.get() != key) {
                bucket_state e;
                do {
                    e = b.state.load(std::memory_order::acquire);
                } while (!b.state.compare_exchange_weak(
                    e, {e.refcount - 1, bucket_state_enum::filled}, std::memory_order::acq_rel,
                    std::memory_order::relaxed));
                continue;
            }

            if (b.state.load(std::memory_order::acquire).state == bucket_state_enum::filled) {
                bucket_state e;
                do {
                    e = b.state.load(std::memory_order::acquire);
                } while (!b.state.compare_exchange_weak(
                    e, {e.refcount - 1, e.state}, std::memory_order::acq_rel, std::memory_order::relaxed));
                return std::unexpected{remove_failed::retry};
            }

            bucket_state e;
            do {
                e = b.state.load(std::memory_order::acquire);
                if (e.refcount > 1) {
                    do {
                        e = b.state.load(std::memory_order::acquire);
                    } while (!b.state.compare_exchange_weak(
                        e, {e.refcount - 1, bucket_state_enum::filled}, std::memory_order::acq_rel,
                        std::memory_order::relaxed));
                    return std::unexpected{remove_failed::guard_protecting};
                }
            } while (!b.state.compare_exchange_weak(
                e, {e.refcount - 1, bucket_state_enum::predestructing}, std::memory_order::acq_rel,
                std::memory_order::relaxed));

            if constexpr (std::is_void_v<V>) {
                b.key.get()->~K();
                if constexpr (!std::is_void_v<V>) {
                    b.value.get()->~V();
                }
                m_load.fetch_sub(1, std::memory_order::relaxed);
                b.state.store({0, bucket_state_enum::tombstone}, std::memory_order::release);

                return std::monostate{};
            } else {
                auto res = std::move(*b.value.get());

                b.key.get()->~K();
                if constexpr (!std::is_void_v<V>) {
                    b.value.get()->~V();
                }
                m_load.fetch_sub(1, std::memory_order::relaxed);
                b.state.store({0, bucket_state_enum::tombstone}, std::memory_order::release);

                return res;
            }
        }

        return std::unexpected{remove_failed::none};
    }

    std::expected<guard, get_failed> do_get(const K &key) {
        auto size = m_buckets.size();
        auto hash = hash1(key);
        auto begindex = hash % size;
        auto step = hash2(key);

        for (std::size_t i = 0; i < size; i++) {
            auto index = (begindex + i * (step % size)) % size;
            bucket &b = m_buckets[index];

            bucket_state e;
            do {
                e = b.state.load(std::memory_order::acquire);
                if (e.state == bucket_state_enum::empty) {
                    return std::unexpected(get_failed::none);
                } else if (
                    e.state == bucket_state_enum::constructing
                    || e.state == bucket_state_enum::predestructing) {
                    return std::unexpected(get_failed::retry);
                } else if (e.state != bucket_state_enum::filled) {
                    goto next;
                }
            } while (!b.state.compare_exchange_weak(
                e, {e.refcount + 1, e.state}, std::memory_order::acq_rel, std::memory_order::relaxed));

            if (*b.key.get() != key) {
                do {
                    e = b.state.load(std::memory_order::acquire);
                } while (!b.state.compare_exchange_weak(
                    e, {e.refcount - 1, e.state}, std::memory_order::acq_rel, std::memory_order::relaxed));
                continue;
            }

            return guard{&b};

        next:
            continue;
        }

        return std::unexpected(get_failed::none);
    }

    void do_rehash() {
        std::vector<bucket> new_buckets(m_buckets.size() * 2);

        for (bucket &b : m_buckets) {
            if (b.state.load(std::memory_order::acquire).state != bucket_state_enum::filled) {
                continue;
            }

            for (std::size_t i = 0, c = b.state.load(std::memory_order::acquire).refcount; c > 0;
                 i++, c = b.state.load(std::memory_order::acquire).refcount) {
                concurrency::exp_withdraw(i);
            }

            auto &key = *b.key.get();
            auto hash = hash1(key);
            auto size = new_buckets.size();
            auto begindex = hash % size;
            auto step = hash2(key);

            for (std::size_t i = 0; i < size; i++) {
                auto index = (begindex + i * (step % size)) % size;
                bucket &newb = new_buckets[index];
                if (newb.state.load(std::memory_order::acquire).state != bucket_state_enum::empty) {
                    continue;
                }

                new (newb.key.get()) K(std::move(key));
                key.~K();
                if constexpr (!std::is_void_v<V>) {
                    new (newb.value.get()) V(std::move(*b.value.get()));
                    b.value.get()->~V();
                }
                b.state.store({0, bucket_state_enum::tombstone}, std::memory_order::release);
                newb.state.store({0, bucket_state_enum::filled}, std::memory_order::release);
                break;
            }
        }

        m_buckets = std::move(new_buckets);
    }

    std::hash<K> m_hasher{};
    std::vector<bucket> m_buckets;
    std::atomic_size_t m_load{0};

    sync::spinrwlock<> m_lock;

    std::size_t hash1(const K &key) { return m_hasher(key); }
    std::size_t hash2(const K &key) {
        return util::detail::mix64(m_hasher(key) ^ 0xd6e8feb86659fd93ull) | 1ull;
    }
};

template<util::types::hash_key K>
using hash_set = hash_map<K, void>;

};  // namespace asco::concurrency
