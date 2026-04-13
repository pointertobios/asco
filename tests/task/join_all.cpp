// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/task/join_all.h>
#include <asco/test/test.h>
#include <asco/yield.h>

#include <atomic>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace asco;

namespace {

future<int> delayed_value(int value, int yield_count, std::atomic_int *completed = nullptr) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    if (completed) {
        completed->fetch_add(1, std::memory_order::acq_rel);
    }
    co_return value;
}

future<void> delayed_void(std::atomic_int &completed, int yield_count) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    completed.fetch_add(1, std::memory_order::acq_rel);
    co_return;
}

future<int> delayed_throw(std::string message, int yield_count) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    throw std::runtime_error{std::move(message)};
}

std::exception_ptr make_runtime_error(std::string_view message) {
    try {
        throw std::runtime_error{std::string{message}};
    } catch (...) { return std::current_exception(); }
}

}  // namespace

ASCO_TEST(join_all_preserves_argument_order_in_result_tuple) {
    auto results = co_await task::join_all{
        []() -> future<int> { co_return co_await delayed_value(10, 2); },
        []() -> future<int> { co_return co_await delayed_value(20, 0); },
        []() -> future<int> { co_return co_await delayed_value(30, 1); },
    };

    auto first = task::fetch(std::move(std::get<0>(results)));
    auto second = task::fetch(std::move(std::get<1>(results)));
    auto third = task::fetch(std::move(std::get<2>(results)));

    ASCO_CHECK(first == 10, "first result should match first argument order, got {}", first);
    ASCO_CHECK(second == 20, "second result should match second argument order, got {}", second);
    ASCO_CHECK(third == 30, "third result should match third argument order, got {}", third);

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_collects_exceptions_without_stopping_other_tasks) {
    std::atomic_int completed{0};

    auto results = co_await task::join_all{
        [&]() -> future<int> { co_return co_await delayed_value(7, 1, &completed); },
        []() -> future<int> { co_return co_await delayed_throw("join_all boom", 1); },
        [&]() -> future<int> { co_return co_await delayed_value(9, 2, &completed); },
    };

    auto first = task::fetch(std::move(std::get<0>(results)));
    auto third = task::fetch(std::move(std::get<2>(results)));
    auto finished = completed.load(std::memory_order::acquire);

    ASCO_CHECK(first == 7, "successful task result should still be available, got {}", first);
    ASCO_CHECK(third == 9, "later successful task result should still be available, got {}", third);
    ASCO_CHECK(finished == 2, "non-throwing tasks should both finish, completion count: {}", finished);

    bool threw_runtime_error = false;
    try {
        (void)task::fetch(std::move(std::get<1>(results)));
    } catch (const std::runtime_error &e) {
        threw_runtime_error = std::string_view{e.what()} == "join_all boom";
    } catch (...) {}

    ASCO_CHECK(threw_runtime_error, "fetch() should rethrow the stored task exception");

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_wraps_void_results_as_expected_monostate) {
    std::atomic_int completed{0};

    auto results = co_await task::join_all{
        [&]() -> future<void> { co_await delayed_void(completed, 1); },
        [&]() -> future<void> { co_await delayed_void(completed, 0); },
    };

    ASCO_CHECK(std::get<0>(results).has_value(), "first void task should produce a successful expected");
    ASCO_CHECK(std::get<1>(results).has_value(), "second void task should produce a successful expected");

    (void)task::fetch(std::move(std::get<0>(results)));
    (void)task::fetch(std::move(std::get<1>(results)));

    auto finished = completed.load(std::memory_order::acquire);
    ASCO_CHECK(finished == 2, "both void tasks should run to completion, completion count: {}", finished);

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_fetch_returns_value_or_rethrows_exception_ptr) {
    auto value = task::fetch(std::expected<int, std::exception_ptr>{42});
    ASCO_CHECK(value == 42, "fetch() should return stored value, got {}", value);

    bool threw_runtime_error = false;
    try {
        (void)task::fetch(
            std::expected<int, std::exception_ptr>{std::unexpected{make_runtime_error("fetch boom")}});
    } catch (const std::runtime_error &e) {
        threw_runtime_error = std::string_view{e.what()} == "fetch boom";
    } catch (...) {}

    ASCO_CHECK(threw_runtime_error, "fetch() should rethrow the exception stored in expected.error()");

    ASCO_SUCCESS();
}