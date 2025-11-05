// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/core/wait_queue.h>
#include <asco/future.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace types;

template<size_t CountMax>
class semaphore_base {
public:
    explicit semaphore_base(size_t initial_count) noexcept
            : count{initial_count} {}

    future<void> acquire() {
        size_t old_count, new_count;
        do {
        fetch_count:
            old_count = count.load(morder::acquire);
            if (!old_count) {
                co_await wq.wait();
                goto fetch_count;
            }
            new_count = old_count - 1;
        } while (!count.compare_exchange_weak(old_count, new_count, morder::acquire, morder::relaxed));
        co_return;
    }

    void release(size_t update = 1) {
        size_t old_count, new_count;
        do {
            old_count = count.load(morder::acquire);
            auto inc = std::min(update, CountMax - old_count);
            new_count = old_count + inc;
        } while (!count.compare_exchange_weak(old_count, new_count, morder::release, morder::relaxed));
        wq.notify(new_count - old_count);
    }

private:
    atomic_size_t count;
    core::wait_queue wq;
};

};  // namespace asco::sync

namespace asco {

using binary_semaphore = sync::semaphore_base<1>;
template<types::size_t CountMax>
using counting_semaphore = sync::semaphore_base<CountMax>;
using unlimited_semaphore = sync::semaphore_base<std::numeric_limits<types::size_t>::max()>;

};  // namespace asco
