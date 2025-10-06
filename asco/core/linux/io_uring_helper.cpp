// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/linux/io_uring_helper.h>

namespace asco::core::_linux {

future_inline<int> peek_uring(uring &ring, size_t seq_num) {
    while (true) {
        if (this_coro::aborted())
            co_return 0;

        if (auto res = ring.peek_or_preattach(seq_num)) {
            co_return std::move(*res);
        } else {
            auto &worker = this_coro::get_worker();
            auto id = this_coro::get_id();
            if (ring.attach(seq_num, id, std::move(res.error()))) {
                if (auto new_res = ring.peek(seq_num))
                    co_return std::move(*new_res);
                worker.sc.suspend(id);
            } else {
                continue;
            }
        }

        co_await std::suspend_always{};
    }
}

};  // namespace asco::core::_linux
