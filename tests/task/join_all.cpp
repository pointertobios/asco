// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <exception>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <asco/future.h>
#include <asco/task/fetch_result.h>
#include <asco/task/join_all.h>
#include <asco/test/test.h>
#include <asco/yield.h>

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

future<void> delayed_void_throw(std::string message, int yield_count) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    throw std::runtime_error{std::move(message)};
}

future<std::string> delayed_string(std::string value, int yield_count, std::atomic_int *completed = nullptr) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    if (completed) {
        completed->fetch_add(1, std::memory_order::acq_rel);
    }
    co_return value;
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

    auto first = task::fetch_result(std::move(std::get<0>(results)));
    auto second = task::fetch_result(std::move(std::get<1>(results)));
    auto third = task::fetch_result(std::move(std::get<2>(results)));

    ASCO_CHECK(first == 10, "first result should match first argument order, got {}", first);
    ASCO_CHECK(second == 20, "second result should match second argument order, got {}", second);
    ASCO_CHECK(third == 30, "third result should match third argument order, got {}", third);

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_supports_single_task) {
    auto results = co_await task::join_all{
        []() -> future<int> { co_return co_await delayed_value(77, 0); },
    };

    auto value = task::fetch_result(std::move(std::get<0>(results)));
    ASCO_CHECK(value == 77, "single-task join_all should return the task value, got {}", value);

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_preserves_mixed_result_types) {
    std::atomic_int completed{0};

    auto results = co_await task::join_all{
        []() -> future<int> { co_return co_await delayed_value(5, 2); },
        [&]() -> future<void> { co_await delayed_void(completed, 1); },
        [&]() -> future<std::string> { co_return co_await delayed_string("joined", 0, &completed); },
    };

    auto first = task::fetch_result(std::move(std::get<0>(results)));
    task::fetch_result(std::move(std::get<1>(results)));
    auto third = task::fetch_result(std::move(std::get<2>(results)));
    auto finished = completed.load(std::memory_order::acquire);

    ASCO_CHECK(first == 5, "first mixed result should contain the int value, got {}", first);
    ASCO_CHECK(third == "joined", "third mixed result should contain the string value, got {}", third);
    ASCO_CHECK(finished == 2, "void and string tasks should both complete, completion count: {}", finished);

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_collects_exceptions_without_stopping_other_tasks) {
    std::atomic_int completed{0};

    auto results = co_await task::join_all{
        [&]() -> future<int> { co_return co_await delayed_value(7, 1, &completed); },
        []() -> future<int> { co_return co_await delayed_throw("join_all boom", 1); },
        [&]() -> future<int> { co_return co_await delayed_value(9, 2, &completed); },
    };

    auto first = task::fetch_result(std::move(std::get<0>(results)));
    auto third = task::fetch_result(std::move(std::get<2>(results)));
    auto finished = completed.load(std::memory_order::acquire);

    ASCO_CHECK(first == 7, "successful task result should still be available, got {}", first);
    ASCO_CHECK(third == 9, "later successful task result should still be available, got {}", third);
    ASCO_CHECK(finished == 2, "non-throwing tasks should both finish, completion count: {}", finished);

    bool threw_runtime_error = false;
    try {
        (void)task::fetch_result(std::move(std::get<1>(results)));
    } catch (const std::runtime_error &e) {
        threw_runtime_error = std::string_view{e.what()} == "join_all boom";
    } catch (...) {}

    ASCO_CHECK(threw_runtime_error, "fetch_result() should rethrow the stored task exception");

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_collects_multiple_exceptions_independently) {
    std::atomic_int completed{0};

    auto results = co_await task::join_all{
        []() -> future<int> { co_return co_await delayed_throw("first join_all boom", 0); },
        [&]() -> future<std::string> { co_return co_await delayed_string("still joined", 1, &completed); },
        []() -> future<int> { co_return co_await delayed_throw("second join_all boom", 2); },
    };

    auto middle = task::fetch_result(std::move(std::get<1>(results)));
    ASCO_CHECK(middle == "still joined", "successful middle task result should be available, got {}", middle);
    ASCO_CHECK(
        completed.load(std::memory_order::acquire) == 1,
        "successful task should complete even when sibling tasks throw");

    bool first_threw = false;
    try {
        (void)task::fetch_result(std::move(std::get<0>(results)));
    } catch (const std::runtime_error &e) {
        first_threw = std::string_view{e.what()} == "first join_all boom";
    } catch (...) {}

    bool second_threw = false;
    try {
        (void)task::fetch_result(std::move(std::get<2>(results)));
    } catch (const std::runtime_error &e) {
        second_threw = std::string_view{e.what()} == "second join_all boom";
    } catch (...) {}

    ASCO_CHECK(first_threw, "first throwing task exception should be stored independently");
    ASCO_CHECK(second_threw, "second throwing task exception should be stored independently");

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_wraps_void_exception_as_unexpected) {
    std::atomic_int completed{0};

    auto results = co_await task::join_all{
        [&]() -> future<void> { co_await delayed_void(completed, 1); },
        []() -> future<void> { co_await delayed_void_throw("join_all void boom", 0); },
        [&]() -> future<int> { co_return co_await delayed_value(33, 2, &completed); },
    };

    task::fetch_result(std::move(std::get<0>(results)));
    auto third = task::fetch_result(std::move(std::get<2>(results)));

    bool void_threw = false;
    try {
        task::fetch_result(std::move(std::get<1>(results)));
    } catch (const std::runtime_error &e) {
        void_threw = std::string_view{e.what()} == "join_all void boom";
    } catch (...) {}

    ASCO_CHECK(void_threw, "void task exception should be stored as unexpected and rethrown");
    ASCO_CHECK(third == 33, "non-void sibling task should still return its value, got {}", third);
    ASCO_CHECK(
        completed.load(std::memory_order::acquire) == 2,
        "successful sibling tasks should both complete when one void task throws");

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

    (void)task::fetch_result(std::move(std::get<0>(results)));
    (void)task::fetch_result(std::move(std::get<1>(results)));

    auto finished = completed.load(std::memory_order::acquire);
    ASCO_CHECK(finished == 2, "both void tasks should run to completion, completion count: {}", finished);

    ASCO_SUCCESS();
}

ASCO_TEST(join_all_fetch_returns_value_or_rethrows_exception_ptr) {
    auto value = task::fetch_result(std::expected<int, std::exception_ptr>{42});
    ASCO_CHECK(value == 42, "fetch_result() should return stored value, got {}", value);

    task::fetch_result(std::expected<void, std::exception_ptr>{});

    bool threw_runtime_error = false;
    try {
        (void)task::fetch_result(
            std::expected<int, std::exception_ptr>{std::unexpected{make_runtime_error("fetch_result boom")}});
    } catch (const std::runtime_error &e) {
        threw_runtime_error = std::string_view{e.what()} == "fetch_result boom";
    } catch (...) {}

    ASCO_CHECK(threw_runtime_error, "fetch_result() should rethrow the exception stored in expected.error()");

    bool threw_void_runtime_error = false;
    try {
        task::fetch_result(
            std::expected<void, std::exception_ptr>{
                std::unexpected{make_runtime_error("fetch_result void boom")}});
    } catch (const std::runtime_error &e) {
        threw_void_runtime_error = std::string_view{e.what()} == "fetch_result void boom";
    } catch (...) {}

    ASCO_CHECK(
        threw_void_runtime_error,
        "fetch_result() should rethrow the exception stored in expected<void>.error()");

    ASCO_SUCCESS();
}