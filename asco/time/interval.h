// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_TIME_INTERVAL_H
#define ASCO_TIME_INTERVAL_H 1

#include <chrono>
#include <coroutine>

#include <asco/future.h>

namespace asco::time {

using namespace std::chrono;

template<typename R = RT>
class interval {
public:
    template<typename Ti>
        requires std::is_same_v<Ti, std::chrono::duration<typename Ti::rep, typename Ti::period>>
    explicit interval(Ti t)
            : duration(duration_cast<nanoseconds>(t)) {}

    future_inline<nanoseconds> tick() {
        // restorer to restore if aboreted after co_return
        struct re {
            interval *self;
            int state{0};
            time_point<high_resolution_clock> last;
            time_point<high_resolution_clock> start;

            re(interval *self)
                    : self(self)
                    , last(self->last)
                    , start(self->start) {}

            ~re() {
                if (!this_coro::aborted())
                    return;

                RT::get_runtime().timer_detach(this_coro::get_id());

                switch (state) {
                case 2:
                    self->last = last;
                case 1:
                    self->start = start;
                    break;
                default:
                    break;
                }
            }
        } restorer{this};

        if (!duration.count()) {
            restorer.state = 0;
            co_return 0ns;
        }

        auto tmp = start;
        start = high_resolution_clock::now();
        auto awake_time = last + duration;

        if (duration < 1us) {
            while (high_resolution_clock::now() < awake_time)
                if (this_coro::aborted()) {
                    start = tmp;
                    restorer.state = 0;
                    co_return this_coro::aborted_value<nanoseconds>;
                }

            restorer.state = 1;
            co_return duration;
        }

        auto &worker = this_coro::get_worker();
        auto id = this_coro::get_id();
        worker.sc.suspend(id);
        RT::get_runtime().timer_attach(id, awake_time);

        co_await std::suspend_always{};

        if (this_coro::aborted()) {
            start = tmp;
            restorer.state = 0;
            co_return this_coro::aborted_value<nanoseconds>;
        }

        auto now = high_resolution_clock::now();
        last = now;

        restorer.state = 2;
        co_return now - start;
    }

private:
    const nanoseconds duration;
    time_point<high_resolution_clock> last{high_resolution_clock::now()};
    time_point<high_resolution_clock> start;
};

};  // namespace asco::time

namespace asco {

using time::interval;

};  // namespace asco

#endif
