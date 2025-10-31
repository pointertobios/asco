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
        spin &s;
        bool none{false};

    public:
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

        guard(guard &&rhs) noexcept
                : s{rhs.s} {
            rhs.none = true;
        }

        ~guard() {
            if (!none)
                s.locked.store(false, morder::release);
        }

        operator bool() const noexcept { return true; }
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
            : value{val} {}

    explicit spin(T &&val)
            : value{std::move(val)} {}

    template<typename... Args>
    explicit spin(Args &&...args)
            : value(std::forward<Args>(args)...) {}

    class guard {
        spin<void>::guard g;
        spin &s;

    public:
        guard(spin &s) noexcept
                : g{s._lock.lock()}
                , s{s} {}

        guard(guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , s{rhs.s} {}

        ~guard() {}

        operator bool() const noexcept { return true; }

        T &operator*() noexcept { return s.value; }

        const T &operator*() const noexcept { return s.value; }

        T *operator->() noexcept { return &s.value; }

        const T *operator->() const noexcept { return &s.value; }
    };

    guard lock() noexcept { return guard{*this}; }

    T &&get() noexcept { return std::move(value); }

private:
    T value;
    spin<void> _lock{};
};

};  // namespace asco::sync

namespace asco {

using sync::spin;

}
