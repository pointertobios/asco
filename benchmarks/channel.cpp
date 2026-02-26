// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <print>
#include <thread>

#include <asco/core/runtime.h>
#include <asco/future.h>
#include <asco/panic.h>
#include <asco/sync/channel.h>
#include <asco/test/bench.h>
#include <asco/yield.h>

namespace {

using asco::future;

future<void> bench_channel_e2e_latency(std::size_t warmup, std::size_t measure) {
    using namespace asco;

    asco::test::bench_context bench{"channel_e2e_latency", warmup, measure};

    auto [ping_tx, ping_rx] = sync::channel<asco::test::span_head>();

    const auto total = warmup + measure;

    auto receiver = spawn([ping_rx = std::move(ping_rx), &bench]() mutable -> future<void> {
        while (auto head = co_await ping_rx.recv()) {
            bench.commit(*head);
        }
        co_return;
    });

    auto sender = spawn([ping_tx = std::move(ping_tx), total, &bench]() mutable -> future<void> {
        for (std::size_t i = 0; i < total; ++i) {
            auto sent = co_await ping_tx.send(bench.get_span());
            co_await this_task::yield();
            if (!sent) {
                break;
            }
        }
        ping_tx.stop();
        co_return;
    });

    co_await sender;
    co_await receiver;
}

}  // namespace

int main() {
    using namespace asco;

    // Keep the runtime small to reduce scheduling noise.
    std::size_t nthreads =
        std::min<std::size_t>(2, std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    core::runtime rt{nthreads};

    constexpr std::size_t warmup = 1'000;
    constexpr std::size_t measure = 100'000;

    try {
        rt.block_on([&]() -> future<void> { co_await bench_channel_e2e_latency(warmup, measure); });
        return 0;
    } catch (...) {
        std::println("unknown exception");
        return 1;
    }
}
