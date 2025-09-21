// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_BARRIER_H
#define ASCO_SYNC_BARRIER_H 1

#include <asco/future.h>
#include <asco/sync/rwspin.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/pubusing.h>

namespace asco::sync {

template<size_t N>
class barrier {
public:
    class token {
    public:
        token(size_t generation, bool last_arriver, barrier &self)
                : generation(generation)
                , last_arriver(last_arriver)
                , self(self) {}

        future_inline<void> wait() {
            if (last_arriver)
                co_return;

            if (self.generation.load(morder::acquire) != generation)
                co_return;

            co_await self.sem.acquire();
            co_return;
        }

    private:
        size_t generation;
        bool last_arriver;
        barrier &self;
    };

    token arrive() {
        while (count.load() == N);
        auto index = count.fetch_add(1, morder::release);

        auto gen = generation.fetch_add(1, morder::acq_rel);
        bool last_arriver = false;
        if (index == N - 1) {
            sem.release(N - 1);
            arrived_sem.release();
            last_arriver = true;
            count.store(0, morder::release);
        }
        return {gen, last_arriver, *this};
    }

    future_inline<void> all_arrived() { co_return co_await arrived_sem.acquire(); }

private:
    atomic_size_t count{0};
    atomic_size_t generation{0};
    semaphore<N - 1> sem{0};
    unlimited_semaphore arrived_sem{0};
};

};  // namespace asco::sync

namespace asco {

using sync::barrier;

};  // namespace asco

#endif
