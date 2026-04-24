// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>
#ifdef ASCO_DEBUG_ENABLED
#    include <thread>
#endif

#include <asco/future.h>
#include <asco/panic.h>
#include <asco/sync/condition_variable.h>

namespace asco::sync {

template<typename T = void>
class rwlock;

template<>
class rwlock<void> final {
    constexpr static std::size_t write_mask = std::size_t(1) << (sizeof(std::size_t) * 8 - 1);
    constexpr static std::size_t write_willing_mask = write_mask >> 1;
    constexpr static std::size_t update_willing_mask = write_mask >> 2;

public:
    class write_guard final {
        friend class rwlock;

    public:
        write_guard() = default;

        ~write_guard() {
            if (!m_rwlock) {
                return;
            }

            m_rwlock->m_state.store(0, std::memory_order::release);
            m_rwlock->m_write_cv.notify();
            m_rwlock->m_read_cv.notify();
        }

        write_guard(const write_guard &) = delete;
        write_guard &operator=(const write_guard &) = delete;

        write_guard(write_guard &&rhs) noexcept
                : m_rwlock{std::move(rhs.m_rwlock)} {
            rhs.m_rwlock = nullptr;
        }
        write_guard &operator=(write_guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~write_guard();
                new (this) write_guard{std::move(rhs)};
            }
            return *this;
        }

        operator bool() const noexcept { return m_rwlock != nullptr; }

    private:
        write_guard(rwlock *p) noexcept
                : m_rwlock{p} {}

        rwlock *m_rwlock{nullptr};
    };

    class read_guard final {
        friend class rwlock;

    public:
        read_guard() = default;

        ~read_guard() {
            if (!m_rwlock) {
                return;
            }

            m_rwlock->m_state.fetch_sub(1, std::memory_order::release);
            m_rwlock->m_read_cv.notify();
            m_rwlock->m_write_cv.notify();
        }

        read_guard(const read_guard &) = delete;
        read_guard &operator=(const read_guard &) = delete;

        read_guard(read_guard &&rhs) noexcept
                : m_rwlock{std::move(rhs.m_rwlock)} {
            rhs.m_rwlock = nullptr;
        }
        read_guard &operator=(read_guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~read_guard();
                new (this) read_guard{std::move(rhs)};
            }
            return *this;
        }

        operator bool() const noexcept { return m_rwlock != nullptr; }

        future<write_guard> upgrade(this read_guard self) {
            if (!self.m_rwlock) {
                co_return write_guard{};
            }

            std::size_t e;
            while (true) {
                e = self.m_rwlock->m_state.load(std::memory_order::acquire);
                if (e & (update_willing_mask | write_willing_mask)) {
                    co_return write_guard{};
                }
                if (self.m_rwlock->m_state.compare_exchange_weak(
                        e, e | write_willing_mask | update_willing_mask, std::memory_order::acq_rel,
                        std::memory_order::acquire)) {
                    break;
                }
                if (e & (update_willing_mask | write_willing_mask)) {
                    co_return write_guard{};
                }
            }

            e = write_willing_mask | update_willing_mask + 1;
            co_await self.m_rwlock->m_write_cv.wait([&] {
                auto res = self.m_rwlock->m_state.compare_exchange_strong(
                    e, write_mask | write_willing_mask, std::memory_order::acq_rel,
                    std::memory_order::acquire);
                e = write_willing_mask | update_willing_mask + 1;
                return res;
            });

            auto rwlock = self.m_rwlock;
            self.m_rwlock = nullptr;
            co_return write_guard{rwlock};
        }

    private:
        read_guard(rwlock *p) noexcept
                : m_rwlock{p} {}

        rwlock *m_rwlock{nullptr};
    };

    rwlock() = default;

    rwlock(const rwlock &) = delete;
    rwlock &operator=(const rwlock &) = delete;

    rwlock(rwlock &&) = delete;
    rwlock &operator=(rwlock &&) = delete;

    future<read_guard> read() noexcept {
        std::size_t e;
        do {
            e = m_state.load(std::memory_order::acquire);
            e = e & ~(write_mask | write_willing_mask);
        } while (co_await m_read_cv.wait_once([&] {
            return m_state.compare_exchange_weak(
                e, e + 1, std::memory_order::acq_rel, std::memory_order::acquire);
        }));
        co_return read_guard{this};
    }

    read_guard try_read() noexcept {
        std::size_t e;
        do {
            e = m_state.load(std::memory_order::acquire);
            if (e & (write_mask | write_willing_mask)) {
                return read_guard{};
            }
            e = e & ~(write_mask | write_willing_mask);
        } while (
            !m_state.compare_exchange_weak(e, e + 1, std::memory_order::acq_rel, std::memory_order::acquire));
        return read_guard{this};
    }

    future<write_guard> write() noexcept {
        std::size_t e;
    start_write:
        do {
            e = m_state.load(std::memory_order::acquire);
        } while (!m_state.compare_exchange_weak(
            e, e | write_willing_mask, std::memory_order::acq_rel, std::memory_order::acquire));

        do {
            do {
                e = m_state.load(std::memory_order::acquire);
                if (e == 0) {
                    goto start_write;
                }
            } while (co_await m_write_cv.wait_once([&] { return e == write_willing_mask; }));
        } while (!m_state.compare_exchange_strong(
            e, e | write_mask, std::memory_order::acq_rel, std::memory_order::acquire));
#ifdef ASCO_DEBUG_ENABLED
        m_writer_id = std::this_thread::get_id();
#endif
        co_return write_guard{this};
    }

    write_guard try_write() noexcept {
        std::size_t e = m_state.load(std::memory_order::acquire);
        if (e) {
            return write_guard{};
        }
        if (!m_state.compare_exchange_strong(
                e, write_mask, std::memory_order::acq_rel, std::memory_order::acquire)) {
            return write_guard{};
        }
#ifdef ASCO_DEBUG_ENABLED
        m_writer_id = std::this_thread::get_id();
#endif
        return write_guard{this};
    }

private:
    condition_variable m_read_cv;
    condition_variable m_write_cv;
    std::atomic_size_t m_state{0};
#ifdef ASCO_DEBUG_ENABLED
    std::thread::id m_writer_id;
#endif
};

template<typename T>
class rwlock final {
public:
    class write_guard final {
        friend class rwlock;
        friend class read_guard;

    public:
        write_guard() = default;

        ~write_guard() = default;

        write_guard(const write_guard &) = delete;
        write_guard &operator=(const write_guard &) = delete;

        write_guard(write_guard &&rhs) noexcept
                : m_rwlock{std::move(rhs.m_rwlock)}
                , m_guard{std::move(rhs.m_guard)} {
            rhs.m_rwlock = nullptr;
        }
        write_guard &operator=(write_guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~write_guard();
                new (this) write_guard{std::move(rhs)};
            }
            return *this;
        }

        operator bool() const noexcept { return m_rwlock != nullptr; }

        const T &operator*() const {
            if (!m_rwlock) {
                panic("asco::sync::rwlock: 解引用失败，空的守卫");
            }
            return m_rwlock->m_value;
        }

        T &operator*() {
            if (!m_rwlock) {
                panic("asco::sync::rwlock: 解引用失败，空的守卫");
            }
            return m_rwlock->m_value;
        }

        const T *operator->() const {
            if (!m_rwlock) {
                panic("asco::sync::rwlock: 解引用失败，空的守卫");
            }
            return &m_rwlock->m_value;
        }

        T *operator->() {
            if (!m_rwlock) {
                panic("asco::sync::rwlock: 解引用失败，空的守卫");
            }
            return &m_rwlock->m_value;
        }

    private:
        write_guard(rwlock *p, rwlock<>::write_guard &&guard) noexcept
                : m_rwlock{p}
                , m_guard{std::move(guard)} {}

        rwlock *m_rwlock{nullptr};
        rwlock<>::write_guard m_guard;
    };

    class read_guard final {
        friend class rwlock;

    public:
        read_guard() = default;

        ~read_guard() = default;

        read_guard(const read_guard &) = delete;
        read_guard &operator=(const read_guard &) = delete;

        read_guard(read_guard &&rhs) noexcept
                : m_rwlock{std::move(rhs.m_rwlock)}
                , m_guard{std::move(rhs.m_guard)} {
            rhs.m_rwlock = nullptr;
        }
        read_guard &operator=(read_guard &&rhs) noexcept {
            if (this != &rhs) {
                this->~read_guard();
                new (this) read_guard{std::move(rhs)};
            }
            return *this;
        }

        operator bool() const noexcept { return m_rwlock != nullptr; }

        future<write_guard> upgrade(this read_guard self) {
            if (!self.m_rwlock) {
                co_return write_guard{};
            }

            auto guard = co_await std::move(self.m_guard).upgrade();
            auto rwlock = const_cast<class rwlock *>(self.m_rwlock);
            self.m_rwlock = nullptr;
            co_return write_guard{rwlock, std::move(guard)};
        }

        const T &operator*() const {
            if (!m_rwlock) {
                panic("asco::sync::rwlock: 解引用失败，空的守卫");
            }
            return m_rwlock->m_value;
        }

        const T *operator->() const {
            if (!m_rwlock) {
                panic("asco::sync::rwlock: 解引用失败，空的守卫");
            }
            return &m_rwlock->m_value;
        }

    private:
        read_guard(const rwlock *p, rwlock<>::read_guard &&guard) noexcept
                : m_rwlock{p}
                , m_guard{std::move(guard)} {}

        const rwlock *m_rwlock{nullptr};
        rwlock<>::read_guard m_guard;
    };

    rwlock()
        requires(!std::is_same_v<std::remove_cvref_t<T>, rwlock> && std::is_default_constructible_v<T>)
    = default;

    template<typename... Args>
    rwlock(Args &&...args)
        requires(!std::is_same_v<std::remove_cvref_t<T>, rwlock>)
            : m_value{args...} {}

    rwlock(T &&value)
        requires(!std::is_same_v<std::remove_cvref_t<T>, rwlock> && std::is_move_constructible_v<T>)
            : m_value{std::move(value)} {}

    ~rwlock() = default;

    rwlock(const rwlock &) = delete;
    rwlock &operator=(const rwlock &) = delete;

    rwlock(rwlock &&) = delete;
    rwlock &operator=(rwlock &&) = delete;

    future<read_guard> read() { co_return read_guard{this, co_await m_rwlock.read()}; }

    read_guard try_read() {
        if (auto guard = m_rwlock.try_read()) {
            return read_guard{this, std::move(guard)};
        } else {
            return read_guard{};
        }
    }

    future<write_guard> write() { co_return write_guard{this, co_await m_rwlock.write()}; }

    write_guard try_write() {
        if (auto guard = m_rwlock.try_write()) {
            return write_guard{this, std::move(guard)};
        } else {
            return write_guard{};
        }
    }

private:
    T m_value;
    rwlock<> m_rwlock;
};

};  // namespace asco::sync
