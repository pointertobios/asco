// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <asco/cancellation.h>
#include <asco/future.h>
#include <asco/task/fetch_result.h>
#include <asco/task/select.h>
#include <asco/test/test.h>
#include <asco/yield.h>

using namespace asco;

namespace {

future<int> delayed_value(int value, int yield_count) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    co_return value;
}

future<void> delayed_void(std::atomic_bool *completed, int yield_count) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    if (completed) {
        completed->store(true, std::memory_order::release);
    }
    co_return;
}

future<int> delayed_throw(std::string message, int yield_count) {
    for (int i = 0; i < yield_count; ++i) {
        co_await this_task::yield();
    }
    throw std::runtime_error{std::move(message)};
}

future<int> never_ready_until_cancelled(std::atomic_bool &cancelled) {
    cancel_callback cb{[&]() { cancelled.store(true, std::memory_order::release); }};
    while (true) {
        co_await this_task::yield();
    }
}

}  // namespace

ASCO_TEST(select_returns_first_completed_branch) {
    auto result = co_await task::select{
        []() -> future<int> { co_return co_await delayed_value(10, 2); },
        []() -> future<int> { co_return co_await delayed_value(20, 0); },
        []() -> future<int> { co_return co_await delayed_value(30, 1); },
    };

    bool visited = false;
    std::size_t selected_index = static_cast<std::size_t>(-1);
    int selected_value = 0;
    std::visit(
        [&](auto &branch) {
            visited = true;
            selected_index = branch.index;
            selected_value = task::fetch_result(std::move(branch.value));
        },
        result);

    ASCO_CHECK(visited, "select result should contain one completed branch");
    ASCO_CHECK(
        selected_index == 1, "select should return the first completed branch index, got {}", selected_index);
    ASCO_CHECK(
        selected_value == 20, "select should return the first completed branch value, got {}",
        selected_value);

    ASCO_SUCCESS();
}

ASCO_TEST(select_returns_exception_from_first_completed_branch) {
    auto result = co_await task::select{
        []() -> future<int> { co_return co_await delayed_value(10, 2); },
        []() -> future<int> { co_return co_await delayed_throw("select boom", 0); },
    };

    bool threw_runtime_error = false;
    std::size_t selected_index = static_cast<std::size_t>(-1);
    std::visit(
        [&](auto &branch) {
            selected_index = branch.index;
            try {
                (void)task::fetch_result(std::move(branch.value));
            } catch (const std::runtime_error &e) {
                threw_runtime_error = std::string_view{e.what()} == "select boom";
            } catch (...) {}
        },
        result);

    ASCO_CHECK(selected_index == 1, "select should return the throwing branch index, got {}", selected_index);
    ASCO_CHECK(threw_runtime_error, "select should store and rethrow the first completed branch exception");

    ASCO_SUCCESS();
}

ASCO_TEST(select_wraps_void_branch_as_monostate) {
    std::atomic_bool completed{false};

    auto result = co_await task::select{
        [&]() -> future<void> { co_await delayed_void(&completed, 0); },
        []() -> future<int> { co_return co_await delayed_value(7, 1); },
    };

    bool visited = false;
    std::size_t selected_index = static_cast<std::size_t>(-1);
    bool void_value_success = false;
    std::visit(
        [&](auto &branch) {
            visited = true;
            selected_index = branch.index;
            using branch_type = std::remove_cvref_t<decltype(branch)>;
            if constexpr (branch_type::index == 0) {
                task::fetch_result(std::move(branch.value));
                void_value_success = true;
            }
        },
        result);

    ASCO_CHECK(visited, "select result should contain one completed branch");
    ASCO_CHECK(
        selected_index == 0, "select should return the completed void branch index, got {}", selected_index);
    ASCO_CHECK(void_value_success, "select should represent successful void result as expected<void>");
    ASCO_CHECK(completed.load(std::memory_order::acquire), "selected void branch should have completed");

    ASCO_SUCCESS();
}

ASCO_TEST(select_cancels_unfinished_branches_after_first_completion) {
    std::atomic_bool cancelled{false};

    auto result = co_await task::select{
        []() -> future<int> { co_return co_await delayed_value(42, 1); },
        [&]() -> future<int> { co_return co_await never_ready_until_cancelled(cancelled); },
    };

    std::size_t selected_index = static_cast<std::size_t>(-1);
    int selected_value = 0;
    std::visit(
        [&](auto &branch) {
            selected_index = branch.index;
            selected_value = task::fetch_result(std::move(branch.value));
        },
        result);

    ASCO_CHECK(
        selected_index == 0, "select should return the completed branch index, got {}", selected_index);
    ASCO_CHECK(
        selected_value == 42, "select should return the completed branch value, got {}", selected_value);
    ASCO_CHECK(
        cancelled.load(std::memory_order::acquire),
        "select should request cancellation for unfinished branches after one branch completes");

    ASCO_SUCCESS();
}