// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/sync/condition_variable.h"

#include <vector>

#include "asco/core/worker.h"

namespace asco::sync {

condition_variable::condition_variable() noexcept {
    auto guard = condition_variable::s_parking_lot.write();
    guard->emplace(this, std::vector<awake_token>{});
}

condition_variable::~condition_variable() noexcept {
    auto guard = condition_variable::s_parking_lot.write();
    guard->remove(this);
}

bool condition_variable::notify_one() noexcept {
    std::optional<core::awake_token> at;
    {
        auto g = m_suspend_lock.lock();
        if (auto tk = m_fast_awake_token.exchange(awake_token::none(), morder::acq_rel)) {
            at = std::move(tk);
        } else {
            auto guard = condition_variable::s_parking_lot.read();
            if (auto v = guard->get(this)) {
                if (!v->empty()) {
                    auto tk_ = v->back();
                    v->pop_back();
                    at = std::move(tk_);
                }
            }
        }
    }
    if (at.has_value()) {
        at->awake();
    }
    return at.has_value();
}

usize condition_variable::notify_all() noexcept {
    std::vector<awake_token> av;
    {
        auto g = m_suspend_lock.lock();
        if (auto tk = m_fast_awake_token.exchange(awake_token::none(), morder::acq_rel)) {
            av.push_back(tk);
        }
        auto guard = condition_variable::s_parking_lot.read();
        if (auto v = guard->get(this)) {
            while (!v->empty()) {
                auto tk = v->back();
                v->pop_back();
                av.push_back(tk);
            }
        }
    }
    for (auto &tk : av) {
        tk.awake();
    }
    return av.size();
}

};  // namespace asco::sync
