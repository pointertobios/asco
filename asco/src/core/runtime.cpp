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
    *m_acceptible_worker_rx.lock() = std::move(acceptible_rx);
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

    std::tie(m_acceptible_blocking_worker_tx, *m_acceptible_blocking_worker_rx.lock()) =
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

bool runtime::spawn_impl(usize target_worker_id, task_item ta) {
    const auto x = target_worker_id;

    if (m_multi_threaded) {
        if (!m_senders[x].send(try_move(ta))) {
            return false;
        }
    } else {
        if (!m_senders[x].send(try_move(ta))) {
            m_workers[x]->fetch_task();
            (void)m_senders[x].send(try_move(ta));
        }
    }
    m_workers[x]->awake();

    return true;
}

usize runtime::get_most_available_worker_id() {
    if (const auto wid = m_acceptible_worker_rx.lock()->recv(); wid.has_value()) {
        return wid.value();
    }
    thread_local std::uniform_int_distribution<usize> tls_rand_worker{0, m_blocking_pool_start - 1};
    return tls_rand_worker(util::rng());
}

usize runtime::get_or_create_id_map_of_worker_handle(worker_handle &handle) {
    while (true) {
        auto rg = m_worker_handle_id_map.read();
        if (const auto id = rg->get(handle.m_handle)) {
            return *id;
        }
        auto wg = std::move(rg).upgrade();
        if (!wg) {
            continue;
        }

        const auto x = get_most_available_worker_id();
        wg->insert(handle.m_handle, x);
        return x;
    }
}

worker_handle &runtime::get_worker_handle_of_current_worker() {
    thread_local worker_handle handle;
    auto wid = worker::current().id();
    while (true) {
        auto rg = m_worker_handle_id_map.read();
        if (const auto _ = rg->get(handle.m_handle)) {
            return handle;
        }
        auto wg = std::move(rg).upgrade();
        if (!wg) {
            continue;
        }
        wg->insert(handle.m_handle, wid);
        return handle;
    }
}

void runtime::main_loop() const {
    ASCO_ASSERT(!m_multi_threaded);

    auto st = m_stop.get_token();
    m_workers[0]->run(st);
}

void runtime::stop() {
    ASCO_ASSERT(!m_multi_threaded);

    (void)m_stop.request_stop();
}

};  // namespace asco::core
