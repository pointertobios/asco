// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/concurrency/concurrency.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace types;

template<typename T = void>
class spin;

template<>
class spin<void> {
public:
    spin() = default;

    spin(const spin &) = delete;
    spin(spin &&) = delete;
    class guard {
        friend spin;

        spin &s;
        bool none{false};

        guard(spin &s) noexcept
                : s{s} {
            size_t withdraw_count{0};
            for (bool b{false}; !s.locked.compare_exchange_weak(b, true, morder::acq_rel, morder::relaxed);
                 b = false) {
                while (s.locked.load(morder::acquire)) {
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
        guard(guard &&rhs) noexcept
                : s{rhs.s}
                , none{rhs.none} {
            rhs.none = true;
        }

        ~guard() {
            if (!none)
                s.locked.store(false, morder::release);
        }

        operator bool() const noexcept { return !none; }
    };

    guard lock() noexcept { return guard{*this}; }

private:
    atomic_bool locked{false};
};

template<typename T>
class spin {
public:
    spin() = default;

    spin(const spin &) = delete;
    spin(spin &&) = delete;

    explicit spin(const T &val)
        requires std::copy_constructible<T>
            : value{val} {}

    explicit spin(T &&val)
        requires std::move_constructible<T>
            : value{std::move(val)} {}

    template<typename... Args>
    explicit spin(Args &&...args)
        requires std::constructible_from<T, Args...>
            : value(std::forward<Args>(args)...) {}

    class guard {
        friend spin;

        spin<>::guard g;
        spin &s;
        bool none{false};

        guard(spin &s) noexcept
                : g{s._lock.lock()}
                , s{s} {}

    public:
        guard(guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , s{rhs.s}
                , none{rhs.none} {
            rhs.none = true;
        }

        operator bool() const noexcept { return !none; }

        T &operator*() noexcept { return s.value; }

        const T &operator*() const noexcept { return s.value; }

        T *operator->() noexcept { return &s.value; }

        const T *operator->() const noexcept { return &s.value; }
    };

    guard lock() noexcept { return guard{*this}; }

    T &&get() noexcept
        requires std::move_constructible<T> || std::is_move_assignable_v<T>
    {
        value_moved.store(true, morder::relaxed);
        return std::move(value);
    }

private:
    T value{};
    atomic_bool value_moved{false};
    spin<> _lock{};
};

};  // namespace asco::sync

namespace asco {

using sync::spin;

};  // namespace asco
