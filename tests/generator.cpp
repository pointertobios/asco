// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <exception>

#include <asco/future.h>
#include <asco/generator.h>

using asco::future;
using asco::future_inline;
using asco::generator;

generator<int> gen_count(int n) {
    for (int i = 1; i <= n; ++i) { co_yield std::move(i); }
    co_return;
}

generator<int> gen_then_throw() {
    co_yield 42;
    throw std::runtime_error("boom");
}

future_inline<int> consume_sum(generator<int> &g) {
    int sum = 0;
    while (auto e = co_await g()) { sum += *e; }
    co_return sum;
}

future<int> async_main() {
    {
        auto g = gen_count(1000);
        auto sum = co_await consume_sum(g);
        assert(sum == 1000 * 1001 / 2);
        assert(!g);
    }

    {
        auto g = gen_then_throw();
        auto first = co_await g();
        assert(first == 42);
        bool got_exc = false;
        try {
            (void)co_await g();
        } catch (const std::exception &) { got_exc = true; }
        assert(got_exc);
        assert(!g);
    }

    {
        auto g1 = gen_count(10);
        for (int i = 1; i <= 3; ++i) {
            auto e = co_await g1();
            assert(*e == i);
        }
        auto g2 = std::move(g1);
        int next_expected = 4;
        while (auto e = co_await g2()) { assert(*e == next_expected++); }
        assert(next_expected == 11);
    }

    co_return 0;
}
