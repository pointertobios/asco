// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>

#include <asco/channel.h>
#include <asco/future.h>

using asco::future, asco::ss::channel;

future<void> sending(asco::sender<int> tx) {
    for (int i = 0; i < 30000; i++) {
        auto r = tx.send(i);
        if (r.has_value())
            break;
    }
    co_return;
}

future<int> async_main() {
    auto [tx, rx] = channel<int>();
    assert(!rx.try_recv());
    assert(!rx.try_recv());
    assert(!rx.try_recv());
    assert(!rx.try_recv());
    assert(!rx.try_recv());
    sending(std::move(tx));
    for (std::optional<int> i = co_await rx.recv(); i.has_value(); i = co_await rx.recv()) {
        std::cout << *i << std::endl;
    }
    co_return 0;
}
