// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/core/runtime.h"

#include <ranges>

#include "asco/assert.h"

namespace asco::core {

runtime runtime_config::build() && { return runtime{*this}; }

std::unique_ptr<runtime> runtime_config::build_ptr() && { return std::make_unique<runtime>(*this); }

runtime_config runtime_config::single_threaded() && {
    m_multi_thread = false;
    m_concurrency = 1;
    return *this;
}

runtime_config runtime_config::multi_threaded(usize n) && {
    m_multi_thread = true;
    m_concurrency = n;
    return *this;
}

runtime::runtime()
        : runtime{runtime_config{}} {}

runtime::runtime(runtime_config config)
        : m_multi_threaded{config.m_multi_thread}
        , m_blocking_pool_start{config.m_concurrency}
        , m_worker_id_gen{config.m_concurrency} {
#ifdef ASCO_DEBUG_ENABLED
    m_debug_host->start();
#endif

    auto [acceptible_tx, acceptible_rx] = concurrency::mpsc<usize>::queue();
    m_acceptible_worker_rx = std::move(acceptible_rx);
    for (usize i : std::views::iota((usize)0, config.m_concurrency)) {
        auto [tx, rx] = concurrency::mpsc<task_item>::queue();
        m_senders.emplace_back(std::move(tx));
        auto atx = acceptible_tx;
        m_workers.emplace_back(std::make_unique<worker>(this, i, std::move(rx), std::move(atx)));
    }
    if (config.m_multi_thread) {
        for (auto &w : m_workers) {
            w->start();
        }
    }

    std::tie(m_acceptible_blocking_worker_tx, m_acceptible_blocking_worker_rx) =
        concurrency::mpsc<usize>::queue();
}

runtime::~runtime() {
    for (auto &w : m_workers) {
        w->join();
    }
    for (auto &w : *m_blocking_workers.read()) {
        w.m_worker->join();
    }

#ifdef ASCO_DEBUG_ENABLED
    m_debug_host->join();
#endif
}

void runtime::main_loop() {
    ASCO_ASSERT(!m_multi_threaded);

    auto st = m_stop.get_token();
    m_workers[0]->run(st);
}

void runtime::stop() {
    ASCO_ASSERT(!m_multi_threaded);

    m_stop.request_stop();
}

};  // namespace asco::core
