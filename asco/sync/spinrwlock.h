// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#ifdef LOCKS_DEBUG
#    include <thread>
#endif

#include <asco/concurrency/concurrency.h>
#include <asco/panic.h>

namespace asco::sync {

template<typename T = void>
class spinrwlock;

template<>
class spinrwlock<> final {
    constexpr static std::size_t write_mask = std::size_t(1) << (sizeof(std::size_t) * 8 - 1);
    constexpr static std::size_t write_willing_mask = write_mask >> 1;

public:
    class read_guard final {
        friend class spinrwlock;

    public:
        read_guard() = default;

        ~read_guard() {
            if (!m_spinrwlock) {
                return;
            }

            m_spinrwlock->m_state.fetch_sub(1, std::memory_order::release);
        }

        read_guard(const read_guard &) = delete;
        read_guard &operator=(const read_guard &) = delete;

        read_guard(read_guard &&rhs) noexcept
                : m_spinrwlock(std::move(rhs.m_spinrwlock)) {
            rhs.m_spinrwlock = nullptr;
        }
        read_guard &operator=(read_guard &&rhs) noexcept {
            if (this != &rhs) {
                m_spinrwlock = rhs.m_spinrwlock;
                rhs.m_spinrwlock = nullptr;
            }
            return *this;
        }

        operator bool() const noexcept { return m_spinrwlock != nullptr; }

    private:
        read_guard(spinrwlock *p) noexcept
                : m_spinrwlock(p) {}

        spinrwlock *m_spinrwlock{nullptr};
    };

    class write_guard final {
        friend class spinrwlock;

    public:
        write_guard() = default;

        ~write_guard() {
            if (!m_spinrwlock) {
                return;
            }

            m_spinrwlock->m_state.store(0, std::memory_order::release);
        }

        write_guard(const write_guard &) = delete;
        write_guard &operator=(const write_guard &) = delete;

        write_guard(write_guard &&rhs) noexcept
                : m_spinrwlock(std::move(rhs.m_spinrwlock)) {
            rhs.m_spinrwlock = nullptr;
        }
        write_guard &operator=(write_guard &&rhs) noexcept {
            if (this != &rhs) {
                m_spinrwlock = rhs.m_spinrwlock;
                rhs.m_spinrwlock = nullptr;
            }
            return *this;
        }

        operator bool() const noexcept { return m_spinrwlock != nullptr; }

    private:
        write_guard(spinrwlock *p) noexcept
                : m_spinrwlock(p) {}

        spinrwlock *m_spinrwlock{nullptr};
    };

    spinrwlock() = default;

    ~spinrwlock() {
        if (m_state.load(std::memory_order::acquire)) {
            panic("asco::sync::spinrwlock: 析构 spinrwlock 时依旧有线程持有锁");
        }
    }

    spinrwlock(const spinrwlock &) = delete;
    spinrwlock &operator=(const spinrwlock &) = delete;

    spinrwlock(spinrwlock &&) = delete;
    spinrwlock &operator=(spinrwlock &&) = delete;

    read_guard read() {
        std::size_t e;
        do {
            e = m_state.load(std::memory_order::acquire);
            e = e & ~(write_mask | write_willing_mask);
        } while (
            !m_state.compare_exchange_weak(e, e + 1, std::memory_order::acq_rel, std::memory_order::acquire));
        return read_guard{this};
    }

    write_guard write() {
        std::size_t e;
        std::size_t lc{0};
        do {
            e = m_state.load(std::memory_order::acquire);
            e = e | write_willing_mask;
            concurrency::exp_withdraw(lc++);
        } while (!m_state.compare_exchange_weak(
            e, e | write_mask, std::memory_order::acq_rel, std::memory_order::acquire));
        e = write_willing_mask;
        do {
            lc = 0;
            while (e != m_state.load(std::memory_order::acquire)) {
                concurrency::exp_withdraw(lc++);
            }
        } while (!m_state.compare_exchange_strong(
            e, e | write_mask, std::memory_order::acq_rel, std::memory_order::acquire));
#ifdef LOCKS_DEBUG
        m_writer_id = std::this_thread::get_id();
#endif
        return write_guard{this};
    }

private:
    std::atomic_size_t m_state{0};
#ifdef LOCKS_DEBUG
    std::thread::id m_writer_id;
#endif
};

template<typename T>
class spinrwlock final {
public:
    class read_guard final {
        friend class spinrwlock;

    public:
        read_guard() = default;

        ~read_guard() = default;

        read_guard(const read_guard &) = delete;
        read_guard &operator=(const read_guard &) = delete;

        read_guard(read_guard &&rhs) noexcept
                : m_spinrwlock(std::move(rhs.m_spinrwlock)) {
            rhs.m_spinrwlock = nullptr;
        }
        read_guard &operator=(read_guard &&rhs) noexcept {
            if (this != &rhs) {
                m_spinrwlock = rhs.m_spinrwlock;
                rhs.m_spinrwlock = nullptr;
            }
            return *this;
        }

        operator bool() const noexcept { return m_spinrwlock != nullptr; }

        const T &operator*() const {
            if (!m_spinrwlock) {
                panic("asco::sync::spinrwlock: 解引用失败，空的守卫");
            }
            return m_spinrwlock->m_value;
        }

        const T *operator->() const {
            if (!m_spinrwlock) {
                panic("asco::sync::spinrwlock: 解引用失败，空的守卫");
            }
            return &m_spinrwlock->m_value;
        }

    private:
        read_guard(spinrwlock *p, spinrwlock<>::read_guard &&guard) noexcept
                : m_spinrwlock{p}
                , m_guard{std::move(guard)} {}

        spinrwlock *m_spinrwlock{nullptr};
        spinrwlock<>::read_guard m_guard;
    };

    class write_guard final {
        friend class spinrwlock;

    public:
        write_guard() = default;

        ~write_guard() = default;

        write_guard(const write_guard &) = delete;
        write_guard &operator=(const write_guard &) = delete;

        write_guard(write_guard &&rhs) noexcept
                : m_spinrwlock(std::move(rhs.m_spinrwlock)) {
            rhs.m_spinrwlock = nullptr;
        }
        write_guard &operator=(write_guard &&rhs) noexcept {
            if (this != &rhs) {
                m_spinrwlock = rhs.m_spinrwlock;
                rhs.m_spinrwlock = nullptr;
            }
            return *this;
        }

        operator bool() const noexcept { return m_spinrwlock != nullptr; }

        const T &operator*() const {
            if (!m_spinrwlock) {
                panic("asco::sync::spinrwlock: 解引用失败，空的守卫");
            }
            return m_spinrwlock->m_value;
        }

        T &operator*() {
            if (!m_spinrwlock) {
                panic("asco::sync::spinrwlock: 解引用失败，空的守卫");
            }
            return m_spinrwlock->m_value;
        }

        const T *operator->() const {
            if (!m_spinrwlock) {
                panic("asco::sync::spinrwlock: 解引用失败，空的守卫");
            }
            return &m_spinrwlock->m_value;
        }

        T *operator->() {
            if (!m_spinrwlock) {
                panic("asco::sync::spinrwlock: 解引用失败，空的守卫");
            }
            return &m_spinrwlock->m_value;
        }

    private:
        write_guard(spinrwlock *p, spinrwlock<>::write_guard &&guard) noexcept
                : m_spinrwlock{p}
                , m_guard{std::move(guard)} {}

        spinrwlock *m_spinrwlock{nullptr};
        spinrwlock<>::write_guard m_guard;
    };

    spinrwlock()
        requires(!std::is_same_v<std::remove_cvref_t<T>, spinrwlock> && std::is_default_constructible_v<T>)
    = default;

    template<typename... Args>
    spinrwlock(Args &&...args)
        requires(!std::is_same_v<std::remove_cvref_t<T>, spinrwlock>)
            : m_value{args...} {}

    spinrwlock(T &&value)
        requires(!std::is_same_v<std::remove_cvref_t<T>, spinrwlock> && std::is_move_constructible_v<T>)
            : m_value{std::move(value)} {}

    ~spinrwlock() = default;

    spinrwlock(const spinrwlock &) = delete;
    spinrwlock &operator=(const spinrwlock &) = delete;

    spinrwlock(spinrwlock &&) = delete;
    spinrwlock &operator=(spinrwlock &&) = delete;

    read_guard read() const { return read_guard{this, m_lock.read()}; }

    write_guard write() { return write_guard{this, m_lock.write()}; }

private:
    T m_value;
    mutable spinrwlock<> m_lock;
};

};  // namespace asco::sync
