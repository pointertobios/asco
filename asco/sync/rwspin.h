// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_RWSPIN_H
#define ASCO_UTILS_RWSPIN_H 1

#include <asco/utils/pubusing.h>

namespace asco::sync {

using namespace types;

template<typename T>
class rwspin {
public:
    rwspin()
            : value{} {}

    rwspin(const rwspin &) = delete;
    rwspin(rwspin &&) = delete;

    explicit rwspin(const T &val)
            : value{val} {}

    explicit rwspin(T &&val)
            : value{std::move(val)} {}

    template<typename... Args>
    explicit rwspin(Args &&...args)
            : value(std::forward<Args>(args)...) {}

    class read_guard {
        rwspin &rw;

    public:
        read_guard(rwspin &rw) noexcept
                : rw{rw} {
            size_t expected;
            do {
                for (expected = rw.state.load(morder::relaxed); (expected & write_mask) != 0;
                     expected = rw.state.load(morder::relaxed)) {}
                // We don't use std::this_thread::yield() because while we use spin locks, the competitors of
                // this lock are largely (almost 100%, because we have cpu affinity for worker threads and
                // task stealing) in different worker threads. There is no need to yield because either we
                // yield or not, the probability of competitors releasing this lock is the same.
            } while (
                !rw.state.compare_exchange_weak(expected, expected + 1, morder::acquire, morder::relaxed));
        }

        ~read_guard() noexcept { rw.state.fetch_sub(1, morder::release); }

        const T &operator*() const noexcept { return rw.value; }

        const T *operator->() const noexcept { return &rw.value; }
    };

    class write_guard {
        rwspin &rw;

    public:
        write_guard(rwspin &rw) noexcept
                : rw{rw} {
            for (size_t expected = 0;
                 !rw.state.compare_exchange_weak(expected, write_mask, morder::acquire, morder::relaxed);
                 expected = 0) {
                while (rw.state.load(morder::acquire)) {}
            }
            // We don't use std::this_thread::yield() because while we use spin locks, the competitors of this
            // lock are largely (almost 100%, because we have cpu affinity for worker threads and task
            // stealing) in different worker threads. There is no need to yield because either we yield or
            // not, the probability of competitors releasing this lock is the same.
        }

        ~write_guard() noexcept { rw.state.store(0, morder::release); }

        T &operator*() noexcept { return rw.value; }
        const T &operator*() const noexcept { return rw.value; }

        T *operator->() noexcept { return &rw.value; }
        const T *operator->() const noexcept { return &rw.value; }
    };

    read_guard read() noexcept { return read_guard{*this}; }
    write_guard write() noexcept { return write_guard{*this}; }

private:
    T value;
    static constexpr size_t write_mask = 1ull << 63;
    atomic_size_t state{0};
};

};  // namespace asco::sync

namespace asco {

using sync::rwspin;

}

#endif
