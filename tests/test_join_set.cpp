// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <print>
#include <vector>

#include <asco/future.h>
#include <asco/join_set.h>
#include <asco/time/sleep.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    std::println("test_join_set: start");

    // Test A: results follow completion order
    {
        join_set<int> set;
        const std::array<std::pair<int, std::chrono::milliseconds>, 3> tasks{{
            {0, 40ms},
            {1, 10ms},
            {2, 20ms},
        }};

        for (auto [value, delay] : tasks) {
            set.spawn([value, delay]() -> future<int> {
                   co_await sleep_for(delay);
                   co_return value;
               })
                .ignore();
        }

        auto results_gen = set.join();
        std::vector<int> observed;
        while (auto value = co_await results_gen()) { observed.push_back(*value); }

        const std::vector<int> expected{1, 2, 0};
        if (observed != expected) {
            std::println(
                "test_join_set: A FAILED - expected completion order [1,2,0], got size {}", observed.size());
            for (size_t idx = 0; idx < observed.size(); ++idx) {
                std::println("  observed[{}] = {}", idx, observed[idx]);
            }
            co_return 1;
        }
        std::println("test_join_set: A passed");
    }

    // Test B: supports functions returning future_spawn
    {
        join_set<int> set;
        set.spawn([]() -> future_spawn<int> { co_return 7; }).ignore();
        set.spawn([]() -> future_spawn<int> {
               co_await sleep_for(1ms);
               co_return 13;
           })
            .ignore();

        auto results_gen = set.join();
        std::vector<int> observed;
        while (auto value = co_await results_gen()) { observed.push_back(*value); }

        if (observed.size() != 2 || observed[0] != 7 || observed[1] != 13) {
            std::println("test_join_set: B FAILED - expected [7,13], got size {}", observed.size());
            co_return 1;
        }
        std::println("test_join_set: B passed");
    }

    // Test C: join on empty set yields nothing
    {
        join_set<int> set;
        auto results_gen = set.join();
        if (auto value = co_await results_gen()) {
            std::println("test_join_set: C FAILED - expected empty join, got {}", *value);
            co_return 1;
        }
        std::println("test_join_set: C passed");
    }

    std::println("test_join_set: all checks passed");
    co_return 0;
}
