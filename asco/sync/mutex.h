// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef LOCKS_DEBUG
#    include <thread>
#endif

#include <asco/core/runtime.h>
#include <asco/future.h>
#include <asco/panic.h>
#include <asco/sync/semaphore.h>

namespace asco::sync {

template<typename T = void>
class mutex;

template<>
class mutex<> final {
public:
    class guard {
        friend class mutex;

    public:
        guard() = default;

        ~guard() {
            if (!m_lock) {
                return;
            }

#ifdef LOCKS_DEBUG
            m_lock->m_locker_id = std::thread::id{0};
#endif
            m_lock->m_locked.release();
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
        guard(mutex<> *lock)
                : m_lock{lock} {}

        mutex<> *m_lock{nullptr};
    };

    mutex() = default;

    ~mutex() {
        if (m_locked.get_count() == 0) {
            panic("asco::sync::mutex: 析构 mutex 时依旧有线程持有锁");
        }
    }

    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;

    mutex(mutex &&rhs) = delete;
    mutex &operator=(mutex &&rhs) = delete;

    future<guard> lock() {
        co_await m_locked.acquire();
#ifdef LOCKS_DEBUG
        m_locker_id = std::this_thread::get_id();
#endif
        co_return guard{this};
    }

    guard blocking_lock() {
        if (in_runtime()) [[unlikely]] {
            panic("asco::sync::mutex: 在 runtime 中禁止使用同步阻塞调用");
        }
        return core::runtime::current().block_on([this]() -> future<guard> { co_return co_await lock(); });
    }

    guard try_lock() noexcept {
        if (m_locked.try_acquire()) {
#ifdef LOCKS_DEBUG
            m_locker_id = std::this_thread::get_id();
#endif
            return guard{this};
        } else {
            return {};
        }
    }

private:
    binary_semaphore m_locked{1};
#ifdef LOCKS_DEBUG
    std::thread::id m_locker_id{0};
#endif
};

template<typename T>
class mutex final {
public:
    class guard {
        friend class mutex;

    public:
        guard() = default;
        ~guard() = default;

        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&rhs)
                : m_lock{rhs.m_lock}
                , m_guard{std::move(rhs.m_guard)} {
            rhs.m_lock = nullptr;
        }

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
                panic("asco::sync::mutex: 解引用失败，空的守卫");
            }
            return m_lock->m_value;
        }

        T &operator*() {
            if (!m_lock) {
                panic("asco::sync::mutex: 解引用失败，空的守卫");
            }
            return m_lock->m_value;
        }

        const T *operator->() const {
            if (!m_lock) {
                panic("asco::sync::mutex: 解引用失败，空的守卫");
            }
            return &m_lock->m_value;
        }

        T *operator->() {
            if (!m_lock) {
                panic("asco::sync::mutex: 解引用失败，空的守卫");
            }
            return &m_lock->m_value;
        }

    private:
        guard(mutex *lock, mutex<>::guard &&guard)
                : m_lock{lock}
                , m_guard{std::move(guard)} {}

        mutex *m_lock{nullptr};
        mutex<>::guard m_guard;
    };

    mutex()
        requires(!std::is_same_v<std::remove_cvref_t<T>, mutex> && std::is_default_constructible_v<T>)
    = default;

    template<typename... Args>
    mutex(Args &&...args)
        requires(!std::is_same_v<std::remove_cvref_t<T>, mutex>)
            : m_value{args...} {}

    mutex(T &&value)
        requires(!std::is_same_v<std::remove_cvref_t<T>, mutex> && std::is_move_constructible_v<T>)
            : m_value{std::move(value)} {}

    ~mutex() = default;

    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;

    mutex(mutex &&) = delete;
    mutex &operator=(mutex &&) = delete;

    future<guard> lock() { co_return {this, co_await m_lock.lock()}; }

    guard blocking_lock() {
        if (in_runtime()) [[unlikely]] {
            panic("asco::sync::mutex: 在 runtime 中禁止使用同步阻塞调用");
        }
        return core::runtime::current().block_on([this]() -> future<guard> { co_return co_await lock(); });
    }

    guard try_lock() noexcept {
        if (auto g = m_lock.try_lock()) {
            return guard{this, std::move(g)};
        } else {
            return {};
        }
    }

private:
    T m_value;
    mutex<> m_lock;
};

};  // namespace asco::sync
