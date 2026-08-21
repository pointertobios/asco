// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <type_traits>

#include "asco/concepts.h"
#include "asco/constants.h"

namespace asco::sync {

template<typename = void>
class spinlock {
public:
    class guard final {
        friend class spinlock;

    public:
        guard() noexcept = default;

        ~guard() noexcept {
            if (!m_spinlock) {
                return;
            }
            m_spinlock->m_locked.store(false, morder::release);
        }

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs) noexcept
                : m_spinlock{rhs.m_spinlock} {
            rhs.m_spinlock = nullptr;
        }

        guard &operator=(guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~guard();
                new (this) guard{std::move(rhs)};
            }
            return *this;
        }

        operator bool() const noexcept { return m_spinlock != nullptr; }

    private:
        guard(spinlock *p) noexcept
                : m_spinlock{p} {}

        spinlock *m_spinlock{nullptr};
    };

    spinlock() noexcept = default;

    spinlock(const spinlock &) = delete;
    spinlock &operator=(const spinlock &) = delete;

    spinlock(spinlock &&) = delete;
    spinlock &operator=(spinlock &&) = delete;

    guard try_lock() noexcept {
        bool e = false;
        if (m_locked.compare_exchange_strong(e, true, morder::acq_rel, morder::acquire)) {
            return guard{this};
        } else {
            return guard{};
        }
    }

    guard lock() noexcept {
        while (true) {
            bool e = false;
            if (m_locked.compare_exchange_weak(e, true, morder::acq_rel, morder::acquire)) {
                return guard{this};
            }
        }
    }

private:
    std::atomic_bool m_locked{false};
};

template<concepts::non_void T>
class spinlock<T> {
public:
    class guard final {
        friend class spinlock;

    public:
        guard() noexcept = default;

        ~guard() noexcept {
            if (!m_spinlock) {
                return;
            }
            m_spinlock->m_locked.store(false, morder::release);
        }

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs) noexcept
                : m_spinlock{rhs.m_spinlock} {
            rhs.m_spinlock = nullptr;
        }

        guard &operator=(guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~guard();
                new (this) guard{std::move(rhs)};
            }
            return *this;
        }

        operator bool() const noexcept { return m_spinlock != nullptr; }

        T &operator*() noexcept { return m_spinlock->m_value; }
        T *operator->() noexcept { return &m_spinlock->m_value; }

    private:
        guard(spinlock *p) noexcept
                : m_spinlock{p} {}

        spinlock *m_spinlock{nullptr};
    };

    spinlock() noexcept(std::is_nothrow_constructible_v<T>)
        requires(std::is_default_constructible_v<T>)
            : m_value{} {}

    template<typename... Args>
        requires std::constructible_from<T, Args...>
    spinlock(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
            : m_value{std::forward<Args>(args)...} {}

    spinlock(const spinlock &) = delete;
    spinlock &operator=(const spinlock &) = delete;

    spinlock(spinlock &&) = delete;
    spinlock &operator=(spinlock &&) = delete;

    guard try_lock() noexcept {
        bool e = false;
        if (m_locked.compare_exchange_strong(e, true, morder::acq_rel, morder::relaxed)) {
            return guard{this};
        } else {
            return guard{};
        }
    }

    guard lock() noexcept {
        while (true) {
            bool e = false;
            if (m_locked.compare_exchange_weak(e, true, morder::acq_rel, morder::relaxed)) {
                return guard{this};
            }
        }
    }

private:
    std::atomic_bool m_locked{false};
    T m_value;
};

};  // namespace asco::sync
