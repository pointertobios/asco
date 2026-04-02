// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/test/test.h>

#include <functional>
#include <optional>
#include <print>
#include <unordered_map>
#include <vector>

#include <asco/core/os/terminal.h>
#include <asco/future.h>
#include <asco/join_set.h>
#include <asco/panic.h>

namespace asco::test {

struct test_info {
    std::string namespace_name;
    std::string name;
    test_function fn;
    bool ignore;
};

struct test_result_wrap {
    std::string namespace_name;
    std::string name;
    test_result result;
};

auto &get_tests() {
    static std::vector<test_info> tests{};
    return tests;
}

bool add_test(std::string_view filename, std::string name, test_function fn, bool ignore) {
    using namespace std::literals;
    auto p1 = filename.rfind("/");
    auto l1 = (p1 == std::string_view::npos) ? filename : filename.substr(p1 + 1);
    auto p2 = l1.rfind("\\");
    auto l2 = (p2 == std::string_view::npos) ? l1 : l1.substr(p2 + 1);
    auto p3 = l2.find(".");
    auto namespace_name = (p3 == std::string_view::npos) ? l2 : l2.substr(0, p3);
    get_tests().emplace_back(std::string{namespace_name}, std::move(name), std::move(fn), ignore);
    return true;
}

};  // namespace asco::test

enum class test_state {
    passed,
    failed,
    ignored,
};

struct test_record {
    test_state state;
    std::string message;
};

int main() {
    using namespace asco;

    core::runtime rt = core::runtime_builder::multi_threaded()  //
                           .with_timer()
                           .build();
    join_set<test::test_result_wrap> set{rt};

    auto total = test::get_tests().size();

    for (auto &[namespace_name, name, fn, ignore] : test::get_tests()) {
        set.spawn([namespace_name, name, fn = std::move(fn), ignore] -> future<test::test_result_wrap> {
            if (ignore) {
                (void)co_await fn();
                co_return {namespace_name, name, std::unexpected{"###ASCO_IGNORED###"}};
            }
            try {
                co_return {namespace_name, name, co_await fn()};
            } catch (panicked &p) {
                co_return {namespace_name, name, std::unexpected{std::format("panic: {}", p.to_string())}};
            } catch (...) { co_return {namespace_name, name, std::unexpected{"发生异常"}}; }
        });
    }

    return rt.block_on([&set, total] -> future<int> {
        std::println();
        std::println();
        auto terminal = core::os::terminal::get();

        std::unordered_map<std::string, std::vector<test_record>> stats_by_namespace;

        std::size_t passed{0};
        std::size_t ignored{0};
        std::optional<decltype(set)::output_type> res;
        while ((res = co_await set)) {
            auto &[namespace_name, name, success] = *res;
            bool is_ignored = !success.has_value() && success.error() == "###ASCO_IGNORED###";
            std::string extra;
            if (!success.has_value() && !is_ignored) {
                extra = ": " + success.error();
            } else if (is_ignored) {
                ignored++;
            } else {
                passed++;
            }
            auto state = success.has_value() ? test_state::passed
                                             : (is_ignored ? test_state::ignored : test_state::failed);
            auto result = success.has_value() ? "通过" : (is_ignored ? "忽略" : "失败");
            auto message = std::format("{}::{}{}", namespace_name, name, extra);
            if (!terminal) {
                std::println("[{}] {}", result, message);
            } else {
                auto cover_lines = stats_by_namespace.size();
                std::print("\033[{}A", cover_lines);
                stats_by_namespace[namespace_name].push_back(test_record{state, message});
                for (auto &[ns, records] : stats_by_namespace) {
                    std::print("{} ", ns);
                    for (auto &[state, _] : records) {
                        switch (state) {
                        case test_state::passed: {
                            std::print("\033[1;32m■\033[0m");
                        } break;
                        case test_state::failed: {
                            std::print("\033[1;31m■\033[0m");
                        } break;
                        case test_state::ignored: {
                            std::print("\033[1;33m■\033[0m");
                        } break;
                        }
                    }
                    std::print("\033[0K");
                    std::println();
                }
            }
        }

        std::println("测试结果：{} 通过，{} 失败，{} 忽略", passed, total - passed - ignored, ignored);

        for (auto &[ns, records] : stats_by_namespace) {
            for (auto &[state, message] : records) {
                if (state == test_state::failed) {
                    std::println("[\033[1;31m失败\033[0m] {}", message);
                }
            }
        }

        co_await set.join_all();

        co_return passed + ignored - total;
    });
}
