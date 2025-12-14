// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/assert.h>
#include <asco/concurrency/concurrency.h>
#include <asco/core/wait_queue.h>
#include <asco/future.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace types;

template<typename T = void>
class mutex;

template<>
class mutex<> {
public:
    mutex() = default;

    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;

    mutex(mutex &&) = delete;
    mutex &operator=(mutex &&) = delete;

    class guard {
        friend mutex;

        mutex &m;
        bool none{false};

        guard(mutex &m)
                : m{m} {}

    public:
        guard(guard &&rhs) noexcept
                : m{rhs.m}
                , none{rhs.none} {
            rhs.none = true;
        }

        ~guard() {
            if (none)
                return;

            m.locked.store(false, morder::release);
            m.wait_queue.notify();
        }

        operator bool() const noexcept { return !none; }
    };

    future<guard> lock() {
        while (true) {
            for (size_t i{0}; locked.load(morder::acquire); i++) {
                if (i < 64) {
                    continue;
                }
                if (i < 64 + 6) {
                    concurrency::exp_withdraw(i - 64);
                    continue;
                }
                co_await wait_queue.wait();
            }

            if (bool b = false; locked.compare_exchange_strong(b, true, morder::acq_rel, morder::relaxed))
                co_return guard{*this};
        }
    }

private:
    atomic_bool locked{false};
    core::wait_queue wait_queue{};
};

template<typename T>
class mutex {
public:
    mutex() = default;

    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;

    mutex(mutex &&) = delete;
    mutex &operator=(mutex &&) = delete;

    explicit mutex(const T &val)
        requires std::copy_constructible<T>
            : value{val} {}

    explicit mutex(T &&val)
        requires std::move_constructible<T>
            : value{std::move(val)} {}

    template<typename... Args>
    explicit mutex(Args &&...args)
        requires std::constructible_from<T, Args...>
            : value{std::forward<Args>(args)...} {}

    class guard {
        friend mutex;

        mutex<>::guard g;
        mutex &m;
        bool none{false};

        guard(mutex<>::guard &&g, mutex &m)
                : g{std::move(g)}
                , m{m} {}

    public:
        guard(guard &&rhs) noexcept
                : g{std::move(rhs.g)}
                , m{rhs.m}
                , none{rhs.none} {
            rhs.none = true;
        }

        operator bool() const noexcept {
            asco_assert(!none);
            return !none;
        }

        T &operator*() noexcept {
            asco_assert(!none);
            return m.value;
        }

        const T &operator*() const noexcept {
            asco_assert(!none);
            return m.value;
        }

        T *operator->() noexcept {
            asco_assert(!none);
            return &m.value;
        }

        const T *operator->() const noexcept {
            asco_assert(!none);
            return &m.value;
        }
    };

    future<guard> lock() {
        asco_assert(!value_moved.load(morder::relaxed));
        co_return guard{co_await mtx.lock(), *this};
    }

    // !!! UNSAFE !!! Ensure there is no guard before calling this
    T &&get() noexcept
        requires std::move_constructible<T> || std::is_move_assignable_v<T>
    {
        value_moved.store(true, morder::relaxed);
        return std::move(value);
    }

private:
    T value{};
    atomic_bool value_moved{false};
    mutex<> mtx;
};

};  // namespace asco::sync

namespace asco {

using sync::mutex;

};  // namespace asco
