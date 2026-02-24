// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <asco/core/runtime.h>
#include <asco/test/test.h>
#include <asco/this_task.h>
#include <asco/yield.h>

using namespace asco;

namespace {

struct tls_int {
    int value;
};

struct basic_result {
    int before;
    int after;
    std::uintptr_t addr_before;
    std::uintptr_t addr_after;
};

struct isolation_result {
    int parent_before;
    int parent_after;
    int child_before;
    int child_after;
};

struct dtor_tls {
    std::atomic_int *dtors{};
    int id{};

    explicit dtor_tls(std::atomic_int &d, int id) noexcept
            : dtors{&d}
            , id{id} {}

    dtor_tls(const dtor_tls &) = delete;
    dtor_tls &operator=(const dtor_tls &) = delete;

    dtor_tls(dtor_tls &&rhs) noexcept
            : dtors{rhs.dtors}
            , id{rhs.id} {
        rhs.id = -1;
    }

    dtor_tls &operator=(dtor_tls &&) = delete;

    ~dtor_tls() {
        if (id != -1 && dtors) {
            dtors->fetch_add(1, std::memory_order::acq_rel);
        }
    }
};

}  // namespace

ASCO_TEST(task_local_basic_read_write_across_yield) {
    auto h = spawn(
        []() -> future<basic_result> {
            auto &tls = this_task::task_local<tls_int>();
            auto addr_before = reinterpret_cast<std::uintptr_t>(&tls);
            auto before = tls.value;

            tls.value += 1;
            co_await this_task::yield();

            auto &tls2 = this_task::task_local<tls_int>();
            auto addr_after = reinterpret_cast<std::uintptr_t>(&tls2);
            auto after = tls2.value;

            co_return basic_result{before, after, addr_before, addr_after};
        },
        tls_int{41});

    auto r = co_await h;

    ASCO_CHECK(r.before == 41, "tls initial value should come from spawn() argument");
    ASCO_CHECK(r.after == 42, "tls writes should be visible after yield()");
    ASCO_CHECK(r.addr_before == r.addr_after, "task_local() should return stable reference within a task");

    ASCO_SUCCESS();
}

ASCO_TEST(task_local_isolated_between_parent_and_child_tasks) {
    auto parent = spawn(
        []() -> future<isolation_result> {
            auto &ptls = this_task::task_local<tls_int>();
            ptls.value = 100;
            auto parent_before = ptls.value;

            auto child = spawn(
                []() -> future<std::pair<int, int>> {
                    auto &ctls = this_task::task_local<tls_int>();
                    auto before = ctls.value;
                    ctls.value += 1;
                    co_await this_task::yield();
                    co_return {before, ctls.value};
                },
                tls_int{200});

            auto [child_before, child_after] = co_await child;

            auto parent_after = this_task::task_local<tls_int>().value;

            co_return isolation_result{parent_before, parent_after, child_before, child_after};
        },
        tls_int{10});

    auto r = co_await parent;

    ASCO_CHECK(r.parent_before == 100, "parent should observe its own TLS");
    ASCO_CHECK(r.parent_after == 100, "child TLS should not affect parent TLS");
    ASCO_CHECK(r.child_before == 200, "child should observe its own TLS");
    ASCO_CHECK(r.child_after == 201, "child TLS writes should persist within child");

    ASCO_SUCCESS();
}

ASCO_TEST(task_local_destroyed_after_join_handle_released) {
    std::atomic_int dtors{0};

    {
        auto h = spawn(
            []() -> future<void> {
                auto &tls = this_task::task_local<dtor_tls>();
                (void)tls;
                co_return;
            },
            dtor_tls{dtors, 1});

        co_await h;
    }

    // TLS 的析构由任务状态的 shared_ptr 引用计数决定：不应假设 join_handle
    // 一离开作用域就“立刻”析构；这里只验证它最终会析构且只析构一次。
    for (std::size_t i = 0; i < 4096 && dtors.load(std::memory_order::acquire) == 0; i++) {
        co_await this_task::yield();
    }

    auto d = dtors.load(std::memory_order::acquire);
    ASCO_CHECK(d == 1, "TLS destructor should run exactly once, run count: {}", d);

    ASCO_SUCCESS();
}
