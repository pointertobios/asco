// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <exception>
#include <print>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <asco/concurrency/hash_map.h>
#include <asco/panic.h>

namespace {

struct test_failure : std::exception {
    const char *what() const noexcept override { return "test failed"; }
};

[[noreturn]] void fail(const char *expr, const char *file, int line) {
    std::println("[hash_map test] FAIL: {} @ {}:{}", expr, file, line);
    throw test_failure{};
}

#define CHECK(expr)                          \
    do {                                     \
        if (!(expr))                         \
            fail(#expr, __FILE__, __LINE__); \
    } while (false)

template<typename Fn>
void expect_panic(Fn &&fn) {
    bool panicked = false;
    try {
        std::forward<Fn>(fn)();
    } catch (const asco::detail::panic_testing &) { panicked = true; }
    CHECK(panicked);
}

void test_basic_insert_get_remove() {
    asco::concurrency::hash_map<int, int> m;

    auto r1 = m.insert(1, 11);
    CHECK(r1.has_value());

    {
        auto g = m.get(1);
        CHECK(static_cast<bool>(g));
        CHECK(*g == 11);
    }

    auto removed = m.remove(1);
    CHECK(removed.has_value());
    CHECK(*removed == 11);

    {
        auto g2 = m.get(1);
        CHECK(!static_cast<bool>(g2));
    }
}

void test_hash_set_contains_remove() {
    asco::concurrency::hash_set<long> s;
    CHECK(!s.contains(9));

    auto ins = s.insert(9);
    CHECK(ins);
    CHECK(s.contains(9));
}

void test_rehash_preserves_elements() {
    asco::concurrency::hash_map<int, int> m;
    constexpr int n = 200;
    for (int i = 0; i < n; i++) {
        auto r = m.insert(i, i * 10);
        CHECK(r.has_value());
    }

    for (int i = 0; i < n; i++) {
        auto g = m.get(i);
        CHECK(static_cast<bool>(g));
        CHECK(*g == i * 10);
    }
}

void test_iterator_empty_and_default_behavior() {
    {
        asco::concurrency::hash_map<int, int> m;
        CHECK(m.begin() == m.end());
        for ([[maybe_unused]] auto kv : m) {
            CHECK(false);
        }
    }

    {
        asco::concurrency::hash_map<int, int>::iterator it;
        expect_panic([&] { (void)*it; });
        expect_panic([&] { ++it; });
    }
}

void test_iterator_visits_all_and_allows_value_mutation() {
    asco::concurrency::hash_map<int, int> m;

    constexpr int n = 128;
    for (int i = 0; i < n; ++i) {
        CHECK(m.insert(i, i * 10).has_value());
    }

    std::unordered_map<int, int> seen;
    for (auto p : m) {
        auto &[k, v] = p;

        static_assert(std::is_same_v<decltype(k), const int &>);
        static_assert(std::is_same_v<decltype(v), int &>);

        CHECK(seen.emplace(k, v).second);
        v += 1;  // 验证 iterator 返回的 value 可写
    }
    CHECK(static_cast<int>(seen.size()) == n);

    for (int i = 0; i < n; ++i) {
        auto g = m.get(i);
        CHECK(static_cast<bool>(g));
        CHECK(*g == i * 10 + 1);
    }
}

void test_const_iterator_visits_all_and_is_read_only() {
    asco::concurrency::hash_map<int, int> m;
    constexpr int n = 64;
    for (int i = 0; i < n; ++i) {
        CHECK(m.insert(i, i + 1000).has_value());
    }

    const auto &cm = m;

    std::unordered_map<int, int> seen;
    for (auto [k, v] : cm) {
        static_assert(std::is_same_v<decltype(k), const int &>);
        static_assert(std::is_same_v<decltype(v), const int &>);
        CHECK(seen.emplace(k, v).second);
    }
    CHECK(static_cast<int>(seen.size()) == n);
}

void test_iterator_is_invalidated_by_mutation() {
    asco::concurrency::hash_map<int, int> m;
    CHECK(m.insert(1, 10).has_value());
    CHECK(m.insert(2, 20).has_value());

    auto it = m.begin();
    CHECK(!(it == m.end()));
    (void)*it;  // 先确保可解引用

    CHECK(m.insert(3, 30).has_value());
    expect_panic([&] { (void)*it; });

    // remove 同样会递增 generation，迭代器应失效
    auto it2 = m.begin();
    (void)*it2;
    auto removed = m.remove(2);
    CHECK(removed.has_value());
    expect_panic([&] { ++it2; });
}

void test_hash_set_iterator() {
    asco::concurrency::hash_set<int> s;
    CHECK(s.insert(1));
    CHECK(s.insert(2));
    CHECK(s.insert(3));

    std::unordered_set<int> seen;
    for (auto [k] : s) {
        static_assert(std::is_same_v<decltype(k), const int &>);
        CHECK(seen.emplace(k).second);
    }
    CHECK(seen.size() == 3);
    CHECK(seen.contains(1));
    CHECK(seen.contains(2));
    CHECK(seen.contains(3));
}

void test_move_is_blocked_while_iterator_alive() {
    asco::concurrency::hash_map<int, int> m;
    CHECK(m.insert(1, 2).has_value());
    CHECK(m.insert(3, 4).has_value());

    auto it = m.begin();
    CHECK(!(it == m.end()));
    (void)*it;

    expect_panic([&] {
        auto moved = std::move(m);
        (void)moved;
    });
}
void test_move_is_blocked_while_guard_alive() {
    asco::concurrency::hash_map<int, int> m;
    CHECK(m.insert(1, 2).has_value());
    CHECK(m.insert(3, 4).has_value());

    auto g = m.get(1);
    CHECK(static_cast<bool>(g));
    expect_panic([&] {
        auto moved = std::move(m);
        (void)moved;
    });
}

}  // namespace

int main() {
    test_basic_insert_get_remove();
    test_hash_set_contains_remove();
    test_rehash_preserves_elements();
    test_move_is_blocked_while_guard_alive();
    test_iterator_empty_and_default_behavior();
    test_iterator_visits_all_and_allows_value_mutation();
    test_const_iterator_visits_all_and_is_read_only();
    test_iterator_is_invalidated_by_mutation();
    test_hash_set_iterator();
    test_move_is_blocked_while_iterator_alive();
    return 0;
}
