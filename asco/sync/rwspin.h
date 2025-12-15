// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/concurrency/concurrency.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace types;

template<typename T = void>
class rwspin;

template<>
class rwspin<void> {
public:
    rwspin() = default;
    rwspin(const rwspin &) = delete;
    rwspin(rwspin &&) = delete;

    class read_guard {
        friend rwspin;

        rwspin &rw;
        bool none{false};

        read_guard(rwspin &rw) noexcept
                : rw{rw} {
            size_t expected;
            do {
                for (expected = rw.state.load(morder::relaxed); (expected & write_mask) != 0;
                     expected = rw.state.load(morder::relaxed)) {}
            } while (
                !rw.state.compare_exchange_weak(expected, expected + 1, morder::acq_rel, morder::relaxed));
        }

    public:
        read_guard(read_guard &&rhs) noexcept
                : rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        ~read_guard() noexcept { rw.state.fetch_sub(1, morder::release); }

        operator bool() const noexcept { return !none; }
    };

    class write_guard {
        friend rwspin;

        rwspin &rw;
        bool none{false};

        write_guard(rwspin &rw) noexcept
                : rw{rw} {
            size_t withdraw_count{0};
            for (size_t expected = 0;
                 !rw.state.compare_exchange_weak(expected, write_mask, morder::acq_rel, morder::relaxed);
                 expected = 0) {
                while (rw.state.load(morder::acquire)) {
                    concurrency::exp_withdraw(withdraw_count);
                    withdraw_count++;
                    if (withdraw_count > 16)
                        withdraw_count = 16;
                }
            }
            // We don't use std::this_thread::yield() because while we use spin locks, the competitors of this
            // lock are largely (almost 100%, because we have cpu affinity for worker threads and task
            // stealing) in different worker threads. There is no need to yield because either we yield or
            // not, the probability of competitors releasing this lock is the same.
        }

    public:
        write_guard(write_guard &&rhs) noexcept
                : rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        ~write_guard() noexcept { rw.state.store(0, morder::release); }

        operator bool() const noexcept { return !none; }
    };

    read_guard read() noexcept { return {*this}; }
    write_guard write() noexcept { return {*this}; }

private:
    static constexpr size_t write_mask = 1ull << (8 * sizeof(size_t) - 1);
    atomic_size_t state{0};
};

template<typename T>
class rwspin {
public:
    rwspin()
            : value{} {}

    rwspin(const rwspin &) = delete;
    rwspin(rwspin &&) = delete;

    explicit rwspin(const T &val)
        requires std::copy_constructible<T>
            : value{val} {}

    explicit rwspin(T &&val)
        requires std::move_constructible<T>
            : value{std::move(val)} {}

    template<typename... Args>
    explicit rwspin(Args &&...args)
        requires std::constructible_from<T, Args...>
            : value(std::forward<Args>(args)...) {}

    class read_guard {
        friend rwspin;

        rwspin<>::read_guard g;
        rwspin &rw;
        bool none{false};

        read_guard(rwspin &rw) noexcept
                : g{rw._lock.read()}
                , rw{rw} {}

    public:
        read_guard(read_guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        operator bool() const noexcept { return !none; }

        const T &operator*() const noexcept { return rw.value; }
        const T *operator->() const noexcept { return &rw.value; }
    };

    class write_guard {
        friend rwspin;

        rwspin<>::write_guard g;
        rwspin &rw;
        bool none{false};

        write_guard(rwspin &rw) noexcept
                : g{rw._lock.write()}
                , rw{rw} {}

    public:
        write_guard(write_guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        operator bool() const noexcept { return !none; }

        T &operator*() noexcept { return rw.value; }
        const T &operator*() const noexcept { return rw.value; }

        T *operator->() noexcept { return &rw.value; }
        const T *operator->() const noexcept { return &rw.value; }
    };

    read_guard read() noexcept { return {*this}; }
    write_guard write() noexcept { return {*this}; }

    T &&get() noexcept
        requires std::move_constructible<T> || std::is_move_assignable_v<T>
    {
        value_moved.store(true, morder::relaxed);
        return std::move(value);
    }

private:
    T value;
    atomic_bool value_moved{false};
    rwspin<> _lock;
};

};  // namespace asco::sync

namespace asco {

using sync::rwspin;

};  // namespace asco
