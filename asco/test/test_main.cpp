// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <functional>
#include <optional>
#include <print>

#include <asco/future.h>
#include <asco/join_set.h>
#include <asco/panic.h>
#include <asco/test/test.h>

namespace asco::test {

auto &get_tests() {
    static std::vector<std::tuple<std::string, test_function, bool>> tests{};
    return tests;
}

bool add_test(std::string name, test_function fn, bool ignore) {
    get_tests().emplace_back(std::move(name), std::move(fn), ignore);
    return true;
}

};  // namespace asco::test

int main() {
    using namespace asco;

    core::runtime rt = core::runtime_builder::multi_threaded()  //
                           .with_timer()
                           .build();
    join_set<std::tuple<std::string, test::test_result>> set{rt};

    auto total = test::get_tests().size();

    for (auto &[name, fn, ignore] : test::get_tests()) {
        set.spawn([name, fn = std::move(fn), ignore] -> future<std::tuple<std::string, test::test_result>> {
            if (ignore) {
                (void)co_await fn();
                co_return {name, std::unexpected{"###ASCO_IGNORED###"}};
            }
            try {
                co_return {name, co_await fn()};
            } catch (panicked &p) {
                co_return {name, std::unexpected{std::format("panic: {}", p.to_string())}};
            } catch (...) { co_return {name, std::unexpected{"发生异常"}}; }
        });
    }

    return rt.block_on([&set, total] -> future<int> {
        std::size_t passed{0};
        std::size_t ignored{0};
        std::optional<decltype(set)::output_type> res;
        while ((res = co_await set)) {
            auto &[name, success] = *res;
            bool is_ignored = !success.has_value() && success.error() == "###ASCO_IGNORED###";
            std::string extra;
            if (!success.has_value() && !is_ignored) {
                extra = ": " + success.error();
            } else if (is_ignored) {
                ignored++;
            } else {
                passed++;
            }
            auto result = success.has_value()
                              ? "\033[1;32m通过\033[0m"
                              : (is_ignored ? "\033[1;33m忽略\033[0m" : "\033[1;31m失败\033[0m");
            std::println("[{}] {}{}", result, name, extra);
        }

        std::println("测试结果：{} 通过，{} 失败，{} 忽略", passed, total - passed - ignored, ignored);

        co_await set.join_all();

        co_return passed + ignored - total;
    });
}
