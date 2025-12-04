// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <print>

#include <asco/assert.h>
#include <asco/future.h>
#include <asco/generator.h>

using asco::future;
using asco::future_spawn;
using asco::generator;

generator<int> gen_count(int n) {
    for (int i = 1; i <= n; ++i) { co_yield i; }
    co_return;
}

generator<int> gen_then_throw() {
    co_yield 42;
    throw std::runtime_error("boom");
}

future<int> consume_sum(generator<int> &g) {
    int sum = 0;
    while (auto e = co_await g()) {  //
        sum += *e;
    }
    co_return sum;
}

future<int> consume_sum_by_value(generator<int> g) {
    int sum = 0;
    while (auto e = co_await g()) { sum += *e; }
    co_return sum;
}

future<int> async_main() {
    {
        auto g = gen_count(1000);
        auto sum = co_await consume_sum(g);
        asco_assert(sum == 1000 * 1001 / 2);
        asco_assert(!g);
        std::println("test_generator: full generation passed");
    }

    {
        auto g = gen_then_throw();
        auto first = co_await g();
        asco_assert(first == 42);
        bool got_exc = false;
        try {
            (void)co_await g();
        } catch (const std::exception &) { got_exc = true; }
        asco_assert(got_exc);
        asco_assert(!g);
        std::println("test_generator: exception handling passed");
    }

    {
        auto g1 = gen_count(10);
        for (int i = 1; i <= 3; ++i) {
            auto e = co_await g1();
            asco_assert(*e == i);
        }
        auto g2 = std::move(g1);
        int next_expected = 4;
        while (auto e = co_await g2()) {  //
            asco_assert(*e == next_expected++);
        }
        asco_assert(next_expected == 11);
        std::println("test_generator: move semantics passed");
    }

    {
        auto g = gen_count(8);
        auto piped = g | [](generator<int> gen) -> future<int> {
            int sum = 0;
            while (auto e = co_await gen()) {  //
                sum += *e;
            }
            co_return sum;
        };
        auto sum = co_await piped;
        asco_assert(sum == 8 * 9 / 2);
        asco_assert(!g);
        std::println("test_generator: pipe with lvalue passed");
    }

    {
        auto sum = co_await (gen_count(5) | consume_sum_by_value);
        asco_assert(sum == 5 * 6 / 2);
        std::println("test_generator: pipe with rvalue passed");
    }

    co_return 0;
}
