// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <type_traits>
#ifdef LOCKS_DEBUG
#    include <thread>
#endif

#include <asco/concurrency/concurrency.h>
#include <asco/panic.h>

namespace asco::sync {

template<typename T = void>
class spinlock;

template<>
class spinlock<void> final {
public:
    class guard {
        friend class spinlock<void>;

    public:
        guard() = default;

        ~guard() {
            if (!m_lock) {
                return;
            }

#ifdef LOCKS_DEBUG
            m_lock->m_locker_id = std::thread::id{0};
#endif
            m_lock->m_locked.store(false, std::memory_order::release);
        }

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs)
                : m_lock{rhs.m_lock} {
            rhs.m_lock = nullptr;
        }

        guard &operator=(guard &&rhs) {
            if (this == &rhs) {
                return *this;
            }

            this->~guard();
            return *new (this) guard{std::move(rhs)};
        }

        operator bool() noexcept { return m_lock; }

    private:
        guard(spinlock *lock)
                : m_lock{lock} {}

        spinlock *m_lock{nullptr};
    };

    spinlock() = default;

    ~spinlock() {
        if (m_locked.load(std::memory_order::acquire)) {
            panic("asco::sync::spinlock: 析构 spinlock 时依旧有线程持有锁");
        }
    }

    spinlock(const spinlock &) = delete;
    spinlock &operator=(const spinlock &) = delete;

    spinlock(spinlock &&rhs) = delete;
    spinlock &operator=(spinlock &&rhs) = delete;

    guard lock() noexcept {
        std::size_t lc{0};
        for (  //
            bool b = false;
            !m_locked.compare_exchange_weak(b, true, std::memory_order::acq_rel, std::memory_order::relaxed);
            b = false, lc++) {
            concurrency::exp_withdraw(lc);
        }
#ifdef LOCKS_DEBUG
        m_locker_id = std::this_thread::get_id();
#endif
        return {this};
    }

    guard try_lock() noexcept {
        bool b = false;
        if (m_locked.compare_exchange_strong(
                b, true, std::memory_order::acq_rel, std::memory_order::relaxed)) {
            return {this};
        } else {
            return {};
        }
    }

private:
    std::atomic_bool m_locked{false};
#ifdef LOCKS_DEBUG
    std::thread::id m_locker_id{0};
#endif
};

template<typename T>
class spinlock final {
public:
    class guard {
        friend class spinlock<T>;

    public:
        guard() = default;
        ~guard() = default;

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs)
                : m_lock{rhs.m_lock}
                , m_guard{std::move(rhs.m_guard)} {}

        guard &operator=(guard &&rhs) {
            if (this == &rhs) {
                return *this;
            }

            this->~guard();
            return *new (this) guard{std::move(rhs)};
        }

        operator bool() const { return m_lock; }

        const T &operator*() const {
            if (!m_lock) {
                panic("asco::sync::spinlock: 解引用失败，空的守卫");
            }
            return m_lock->m_value;
        }

        T &operator*() {
            if (!m_lock) {
                panic("asco::sync::spinlock: 解引用失败，空的守卫");
            }
            return m_lock->m_value;
        }

        const T *operator->() const {
            if (!m_lock) {
                panic("asco::sync::spinlock: 解引用失败，空的守卫");
            }
            return &m_lock->m_value;
        }

        T *operator->() {
            if (!m_lock) {
                panic("asco::sync::spinlock: 解引用失败，空的守卫");
            }
            return &m_lock->m_value;
        }

    private:
        guard(spinlock *lock, spinlock<>::guard &&guard)
                : m_lock{lock}
                , m_guard{std::move(guard)} {}

        spinlock *m_lock{nullptr};
        spinlock<>::guard m_guard;
    };

    spinlock()
        requires(std::is_default_constructible_v<T>)
    = default;

    template<typename... Args>
    spinlock(Args &&...args)
            : m_value{args...} {}

    ~spinlock() = default;

    spinlock(const spinlock &) = delete;
    spinlock &operator=(const spinlock &) = delete;

    spinlock(spinlock &&rhs) = delete;
    spinlock &operator=(spinlock &&rhs) = delete;

    guard lock() noexcept { return {this, m_lock.lock()}; }

    guard try_lock() noexcept {
        if (auto g = m_lock.try_lock()) {
            return {this, g};
        } else {
            return {};
        }
    }

private:
    T m_value;
    spinlock<> m_lock;
};

};  // namespace asco::sync
