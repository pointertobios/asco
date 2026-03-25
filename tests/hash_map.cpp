// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/concurrency/hash_map.h>
#include <asco/join_set.h>
#include <asco/test/test.h>
#include <asco/this_task.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct key_int {
    int v{};

    bool operator==(const key_int &) const = default;
};

}  // namespace

template<>
struct std::hash<key_int> {
    std::size_t operator()(const key_int &k) const noexcept { return std::hash<int>{}(k.v); }
};

namespace {

struct throw_on_move {
    throw_on_move() = default;
    throw_on_move(const throw_on_move &) = default;
    throw_on_move &operator=(const throw_on_move &) = default;

    throw_on_move(throw_on_move &&) { throw std::runtime_error{"throw_on_move"}; }

    throw_on_move &operator=(throw_on_move &&) = default;

    ~throw_on_move() noexcept = default;
};

}  // namespace

ASCO_TEST(hash_map_basic_insert_get_remove) {
    using asco::concurrency::get_failed;
    using asco::concurrency::hash_map;

    hash_map<int, int> m;

    auto inserted = m.try_insert(1, 42);
    ASCO_CHECK(inserted.has_value(), "insert failed");

    {
        auto g = m.try_get(1);
        ASCO_CHECK(g.has_value(), "get failed: {}", static_cast<int>(g.error()));
        ASCO_CHECK(g.value().key() == 1, "key mismatch");
        ASCO_CHECK(g.value().value() == 42, "value mismatch");

        g.value().value() = 43;
    }

    auto removed = m.try_remove(1);
    ASCO_CHECK(removed.has_value(), "remove failed");
    ASCO_CHECK(removed.value() == 43, "removed value mismatch");

    auto missing = m.try_get(1);
    ASCO_CHECK(!missing.has_value(), "expected missing key");
    ASCO_CHECK(missing.error() == get_failed::none, "expected get_failed::none");

    ASCO_SUCCESS();
}

ASCO_TEST(hash_map_non_try_wrappers_basic) {
    using asco::concurrency::hash_map;

    hash_map<int, int> m;

    ASCO_CHECK(m.insert(1, 42), "insert should succeed");
    ASCO_CHECK(!m.insert(1, 99), "duplicate insert should fail");

    ASCO_CHECK(m.contains(1), "contains(1) should be true");
    ASCO_CHECK(!m.contains(2), "contains(2) should be false");

    {
        auto g = m.get(1);
        ASCO_CHECK(static_cast<bool>(g), "get(1) should succeed");
        ASCO_CHECK(g.key() == 1, "key mismatch");
        ASCO_CHECK(g.value() == 42, "value mismatch");
        g.value() = 43;
    }

    {
        auto g = m.get(2);
        ASCO_CHECK(!static_cast<bool>(g), "get(2) should return empty guard");
    }

    {
        auto removed = m.remove(1);
        ASCO_CHECK(removed.has_value(), "remove(1) should succeed");
        ASCO_CHECK(*removed == 43, "removed value mismatch");
    }

    {
        auto removed = m.remove(1);
        ASCO_CHECK(!removed.has_value(), "remove(1) again should be nullopt");
    }

    ASCO_SUCCESS();
}

ASCO_TEST(hash_map_non_try_wrappers_insert_auto_rehash) {
    using asco::concurrency::hash_map;

    hash_map<int, int> m;

    // Fill to just below the observed rehash-needed threshold.
    for (int i = 0; i < 39; ++i) {
        ASCO_CHECK(m.insert(i, i * 10), "insert failed at {}", i);
    }

    // This should trigger internal rehash and eventually succeed.
    ASCO_CHECK(m.insert(39, 390), "insert(39) should succeed after auto rehash");

    for (int i = 0; i < 40; ++i) {
        auto g = m.get(i);
        ASCO_CHECK(static_cast<bool>(g), "get failed at {}", i);
        ASCO_CHECK(g.value() == i * 10, "value mismatch at {}", i);
    }

    ASCO_SUCCESS();
}

ASCO_TEST(hash_map_void_value_non_try_wrappers) {
    using asco::concurrency::get_failed;
    using asco::concurrency::hash_map;

    hash_map<int, void> s;

    ASCO_CHECK(s.insert(7), "set insert should succeed");
    ASCO_CHECK(!s.insert(7), "set duplicate insert should fail");
    ASCO_CHECK(s.contains(7), "contains(7) should be true");
    ASCO_CHECK(!s.contains(8), "contains(8) should be false");

    {
        auto got = s.try_get(7);
        ASCO_CHECK(got.has_value(), "try_get(7) should succeed for set");
    }

    {
        auto got = s.try_get(8);
        ASCO_CHECK(!got.has_value(), "try_get(8) should fail for set");
        ASCO_CHECK(got.error() == get_failed::none, "expected get_failed::none");
    }

    ASCO_CHECK(s.remove(7), "remove(7) should succeed");
    ASCO_CHECK(!s.remove(7), "remove(7) again should be false");
    ASCO_CHECK(!s.contains(7), "contains(7) should be false after remove");

    ASCO_SUCCESS();
}

ASCO_TEST(hash_map_guard_blocks_remove) {
    using asco::concurrency::hash_map;
    using asco::concurrency::remove_failed;

    hash_map<int, int> m;

    ASCO_CHECK(m.try_insert(7, 9).has_value(), "insert failed");

    {
        auto g = m.try_get(7);
        ASCO_CHECK(g.has_value(), "get failed");

        auto removed_while_guard = m.try_remove(7);
        ASCO_CHECK(!removed_while_guard.has_value(), "expected remove to fail while guard held");
        ASCO_CHECK(
            removed_while_guard.error() == remove_failed::guard_protecting,
            "expected remove_failed::guard_protecting");
    }

    auto removed = m.try_remove(7);
    ASCO_CHECK(removed.has_value(), "remove failed after guard released");
    ASCO_CHECK(removed.value() == 9, "removed value mismatch");

    ASCO_SUCCESS();
}

ASCO_TEST(hash_map_rehash_needed_then_rehash) {
    using asco::concurrency::hash_map;
    using asco::concurrency::insert_failed;

    hash_map<int, int> m;

    for (int i = 0; i < 39; i++) {
        auto res = m.try_insert(i, i * 10);
        ASCO_CHECK(res.has_value(), "insert failed at {}", i);
    }

    auto needs = m.try_insert(39, 390);
    ASCO_CHECK(!needs.has_value(), "expected rehash_needed");
    ASCO_CHECK(needs.error() == insert_failed::rehash_needed, "expected insert_failed::rehash_needed");

    ASCO_CHECK(m.try_rehash(), "rehash returned false");

    auto after = m.try_insert(39, 390);
    ASCO_CHECK(after.has_value(), "insert after rehash failed");

    for (int i = 0; i < 40; i++) {
        auto got = m.try_get(i);
        ASCO_CHECK(got.has_value(), "get failed at {}", i);
        ASCO_CHECK(got.value().value() == i * 10, "value mismatch at {}", i);
    }

    ASCO_SUCCESS();
}

ASCO_TEST(hash_map_concurrent_stress, ASCO_IGNORE_TEST) {
    using asco::join_set;
    using asco::concurrency::get_failed;
    using asco::concurrency::hash_map;
    using asco::concurrency::insert_failed;
    using asco::concurrency::remove_failed;

    hash_map<int, int> m;

    constexpr int key_space = 32;
    constexpr auto run_for = 200ms;

    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned thread_count = std::max(2u, std::min(8u, hw == 0 ? 4u : hw));

    std::atomic<bool> stop{false};

    // Stop signal is triggered by a blocking timer.
    auto timer = asco::spawn_blocking([&] {
        std::this_thread::sleep_for(run_for);
        stop.store(true, std::memory_order::release);
    });

    join_set<asco::test::test_result> set;
    for (unsigned t = 0; t < thread_count; ++t) {
        set.spawn([&, t]() -> asco::future<asco::test::test_result> {
            std::uint64_t rng = 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(t) << 1);
            auto next_u64 = [&]() noexcept {
                rng ^= rng << 13;
                rng ^= rng >> 7;
                rng ^= rng << 17;
                return rng;
            };

            while (!stop.load(std::memory_order::relaxed)) {
                const std::uint64_t r = next_u64();
                const int key = static_cast<int>(r % static_cast<std::uint64_t>(key_space));
                const int op = static_cast<int>((r >> 16) % 100);

                if (op < 35) {
                    for (int attempt = 0; attempt < 16; ++attempt) {
                        auto res = m.try_insert(key, 1);
                        if (res.has_value()) {
                            break;
                        }

                        switch (res.error()) {
                        case insert_failed::key_repeated:
                            attempt = 999;
                            break;
                        case insert_failed::rehash_needed:
                        case insert_failed::rehashing:
                        case insert_failed::retry:
                            co_await asco::this_task::yield();
                            break;
                        default:
                            stop.store(true, std::memory_order::release);
                            ASCO_CHECK(false, "unexpected insert_failed");
                            break;
                        }
                    }
                } else if (op < 80) {
                    for (int attempt = 0; attempt < 16; ++attempt) {
                        auto got = m.try_get(key);
                        if (got.has_value()) {
                            // Only read; concurrent mutation is not guaranteed safe.
                            (void)got.value().value();

                            // Occasionally keep the guard across yields to increase contention.
                            if ((r & 0x7) == 0) {
                                co_await asco::this_task::yield();
                                co_await asco::this_task::yield();
                            }
                            break;
                        }

                        switch (got.error()) {
                        case get_failed::none:
                            attempt = 999;
                            break;
                        case get_failed::rehashing:
                        case get_failed::retry:
                            co_await asco::this_task::yield();
                            break;
                        default:
                            stop.store(true, std::memory_order::release);
                            ASCO_CHECK(false, "unexpected get_failed");
                            break;
                        }
                    }
                } else {
                    for (int attempt = 0; attempt < 16; ++attempt) {
                        auto removed = m.try_remove(key);
                        if (removed.has_value()) {
                            break;
                        }

                        switch (removed.error()) {
                        case remove_failed::none:
                            attempt = 999;
                            break;
                        case remove_failed::guard_protecting:
                        case remove_failed::rehashing:
                        case remove_failed::retry:
                            co_await asco::this_task::yield();
                            break;
                        default:
                            stop.store(true, std::memory_order::release);
                            ASCO_CHECK(false, "unexpected remove_failed");
                            break;
                        }
                    }
                }
            }

            ASCO_SUCCESS();
        });
    }

    co_await timer;

    std::optional<asco::test::test_result> out;
    while ((out = co_await set)) {
        if (!out->has_value()) {
            co_return std::unexpected{out->error()};
        }
    }

    // Cleanup & basic post-conditions (single-threaded).
    for (int k = 0; k < key_space; ++k) {
        for (int attempt = 0; attempt < 256; ++attempt) {
            auto removed = m.try_remove(k);
            if (removed.has_value()) {
                continue;
            }

            if (removed.error() == remove_failed::none) {
                break;
            }
            if (removed.error() == remove_failed::retry || removed.error() == remove_failed::rehashing) {
                co_await asco::this_task::yield();
                continue;
            }
            ASCO_CHECK(
                false, "unexpected remove error during cleanup: {}", static_cast<int>(removed.error()));
        }
    }

    ASCO_CHECK(m.try_insert(123456, 1).has_value(), "post-stress insert failed");
    auto g = m.try_get(123456);
    ASCO_CHECK(g.has_value(), "post-stress get failed");
    ASCO_CHECK(g.value().value() == 1, "post-stress value mismatch");

    ASCO_SUCCESS();
}
