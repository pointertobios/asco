// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_BARRIER_H
#define ASCO_SYNC_BARRIER_H 1

#include <asco/channel.h>
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

        future_void_inline wait() {
            if (last_arriver)
                co_return {};

            if (*self.generation.read() != generation)
                co_return {};

            co_await self.sem.acquire();
            co_return {};
        }

    private:
        size_t generation;
        bool last_arriver;
        barrier &self;
    };

    token arrive() {
        while (count.load() == N);
        auto index = count.fetch_add(1);

        auto gen = *generation.read();
        bool last_arriver = false;
        if (index == N - 1) {
            sem.release(N - 1);
            if (auto res = std::get<0>(channel).send((*generation.write())++))
                throw asco::runtime_error("[ASCO] barrier::arrive() inner: channel closed unexpedtedly.");
            last_arriver = true;
            count.store(0);
        }
        return {gen, last_arriver, *this};
    }

    future_inline<size_t> all_arrived() {
        if (auto res = co_await std::get<1>(channel).recv())
            co_return *res;

        throw asco::runtime_error("[ASCO] barrier::all_arrived() inner: channel closed unexpedtedly.");
    }

private:
    atomic_size_t count{0};
    rwspin<size_t> generation{0};
    semaphore<N - 1> sem{0};
    std::tuple<sender<size_t>, receiver<size_t>> channel{ss::channel<size_t>()};
};

};  // namespace asco::sync

namespace asco {

using sync::barrier;

};

#endif
