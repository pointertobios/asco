// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_JOIN_H
#define ASCO_JOIN_H 1

#include <optional>

#include <asco/channel.h>
#include <asco/future.h>
#include <asco/sync/spin.h>
#include <asco/utils/concepts.h>

namespace asco {

template<typename F>
    requires is_async_task<F>
class join {
public:
    using return_type = typename std::invoke_result_t<F>::return_type;

    join(std::initializer_list<F> futures)
            : count{futures.size()} {
        auto [tx, rx] = channel<return_type>();
        this->tx = std::move(tx);
        this->rx = std::move(rx);
        for (auto &f : futures) {
            [this, &f]() -> future_void {
                auto ret = co_await f();
                tx.lock()->send(std::move(ret));
                co_return {};
            }();
        }
    }

    join(const join &) = delete;
    join(join &&) = delete;
    join &operator=(const join &) = delete;
    join &operator=(join &&) = delete;

    operator bool() const { return count != 0; }

    future_inline<std::optional<return_type>> operator()() {
        if (count == 0)
            co_return std::nullopt;

        auto ret = co_await rx.recv();
        if (ret.has_value())
            --count;

        co_return ret;
    }

private:
    spin<sender<return_type>> tx;
    receiver<return_type> rx;
    size_t count;
};

};  // namespace asco

#endif
