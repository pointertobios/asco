// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/concurrency/concurrency.h>
#include <asco/core/wait_queue.h>
#include <asco/future.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace types;

template<typename T = void>
class rwlock;

template<>
class rwlock<> {
public:
    rwlock() = default;

    rwlock(const rwlock &) = delete;
    rwlock &operator=(const rwlock &) = delete;

    rwlock(rwlock &&) = delete;
    rwlock &operator=(rwlock &&) = delete;

    class read_guard {
        friend rwlock;

        rwlock &rw;
        bool none{false};

        read_guard(rwlock &rw) noexcept
                : rw{rw} {}

    public:
        read_guard(read_guard &&rhs) noexcept
                : rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        ~read_guard() {
            if (none)
                return;

            if ((rw.state.fetch_sub(1, morder::release) & ~write_willing_mask) == 1) {
                rw.wq.notify();
            }
        }

        operator bool() const noexcept { return !none; }
    };

    class write_guard {
        friend rwlock;

        rwlock &rw;
        bool none{false};

        write_guard(rwlock &rw) noexcept
                : rw{rw} {}

    public:
        write_guard(write_guard &&rhs) noexcept
                : rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        ~write_guard() {
            if (none)
                return;

            for (  //
                size_t i = rw.state.load(morder::relaxed);
                !rw.state.compare_exchange_weak(i, i & ~write_mask, morder::release, morder::relaxed);
                i = rw.state.load(morder::relaxed));
            rw.rq.notify(std::numeric_limits<size_t>::max(), false);
            rw.wq.notify();
        }

        operator bool() const noexcept { return !none; }
    };

    future<read_guard> read() {
        while (true) {
            size_t state_old;
            for (  //
                size_t i{0}; (state_old = state.load(morder::acquire)) & (write_mask | write_willing_mask);
                i++) {
                if (i < 64) {
                    continue;
                }
                if (i < 64 + 6) {
                    concurrency::exp_withdraw(i - 64);
                    continue;
                }
                co_await rq.wait();
            }
            if (state.compare_exchange_strong(state_old, state_old + 1, morder::acq_rel, morder::relaxed))
                co_return read_guard{*this};
        }
    }

    future<write_guard> write() {
        while (true) {
            for (  //
                size_t i = state.load(morder::relaxed);
                !state.compare_exchange_weak(i, i | write_willing_mask, morder::release, morder::relaxed);
                i = state.load(morder::relaxed));
            size_t state_old;
            for (size_t i{0}; (state_old = state.load(morder::acquire)) & ~write_willing_mask; i++) {
                if (i < 64) {
                    continue;
                }
                if (i < 64 + 6) {
                    concurrency::exp_withdraw(i - 64);
                    continue;
                }
                co_await wq.wait();
            }
            if (state.compare_exchange_strong(state_old, write_mask, morder::acq_rel, morder::relaxed))
                co_return write_guard{*this};
        }
    }

private:
    static constexpr size_t write_mask = 1ull << (8 * sizeof(size_t) - 1);
    static constexpr size_t write_willing_mask = 1ull << (8 * sizeof(size_t) - 2);
    atomic_size_t state{0};
    core::wait_queue rq{};
    core::wait_queue wq{};
};

template<typename T>
class rwlock {
public:
    rwlock()
            : value{} {}

    rwlock(const rwlock &) = delete;
    rwlock &operator=(const rwlock &) = delete;

    rwlock(rwlock &&) = delete;
    rwlock &operator=(rwlock &&) = delete;

    explicit rwlock(const T &val)
        requires std::copy_constructible<T>
            : value{val} {}

    explicit rwlock(T &&val)
        requires std::move_constructible<T>
            : value{std::move(val)} {}

    template<typename... Args>
    explicit rwlock(Args &&...args)
        requires std::constructible_from<T, Args...>
            : value{std::forward<Args>(args)...} {}

    class read_guard {
        friend rwlock;

        rwlock<>::read_guard g;
        rwlock &rw;
        bool none{false};

        read_guard(rwlock<>::read_guard &&g, rwlock &rw) noexcept
                : g{std::move(g)}
                , rw{rw} {}

    public:
        read_guard(read_guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        operator bool() const noexcept { return !none; }

        const T *operator->() const noexcept {
            asco_assert(!none);
            return &rw.value;
        }
        const T &operator*() const noexcept {
            asco_assert(!none);
            return rw.value;
        }
    };

    class write_guard {
        friend rwlock;

        rwlock<>::write_guard g;
        rwlock &rw;
        bool none{false};

        write_guard(rwlock<>::write_guard &&g, rwlock &rw) noexcept
                : g{std::move(g)}
                , rw{rw} {}

    public:
        write_guard(write_guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , rw{rhs.rw}
                , none{rhs.none} {
            rhs.none = true;
        }

        operator bool() const noexcept { return !none; }

        T *operator->() noexcept {
            asco_assert(!none);
            return &rw.value;
        }
        const T *operator->() const noexcept {
            asco_assert(!none);
            return &rw.value;
        }

        T &operator*() noexcept {
            asco_assert(!none);
            return rw.value;
        }
        const T &operator*() const noexcept {
            asco_assert(!none);
            return rw.value;
        }
    };

    future<read_guard> read() {
        asco_assert(!value_moved.load(morder::relaxed));
        co_return read_guard{std::move(co_await _lock.read()), *this};
    }

    future<write_guard> write() {
        asco_assert(!value_moved.load(morder::relaxed));
        co_return write_guard{std::move(co_await _lock.write()), *this};
    }

    // !!! UNSAFE !!! Ensure there is no reader or writer before calling this
    T &&get() noexcept
        requires std::move_constructible<T> || std::is_move_assignable_v<T>
    {
        value_moved.store(true, morder::release);
        return std::move(value);
    }

private:
    T value;
    atomic_bool value_moved{false};
    rwlock<> _lock;
};

};  // namespace asco::sync

namespace asco {

using sync::rwlock;

};  // namespace asco
