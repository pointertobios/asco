// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_MUTEX_H
#define ASCO_SYNC_MUTEX_H 1

#include <asco/future.h>
#include <asco/sync/semaphore.h>

namespace asco::sync {

template<typename T = void>
class mutex;

template<>
class mutex<void> {
public:
    class guard {
    public:
        guard() = default;

        explicit guard(mutex *self)
                : self(self) {}

        guard(guard &&rhs) noexcept
                : self(rhs.self) {
            rhs.moved = true;
        }

        guard &operator=(guard &&rhs) noexcept {
            if (this == &rhs)
                return *this;
            release_if_needed();
            self = rhs.self;
            rhs.moved = true;
            return *this;
        }

        ~guard() { release_if_needed(); }

    private:
        void release_if_needed() {
            if (!self || moved)
                return;
            self->sem.release();
        }

        mutex *self{nullptr};
        bool moved{false};
    };

    mutex() = default;
    mutex(const mutex &) = delete;
    mutex(mutex &&) = delete;

    std::optional<guard> try_lock() {
        if (sem.try_acquire())
            return guard(this);
        return std::nullopt;
    }

    future_inline<guard> lock() {
        struct re {
            mutex *self;
            int state{0};

            ~re() {
                if (!this_coro::aborted())
                    return;

                this_coro::throw_coroutine_abort<future_inline<guard>>();

                if (state == 1)
                    self->sem.release();
            }
        } restorer{this};

        if (this_coro::aborted())
            throw coroutine_abort{};

        co_await sem.acquire();

        if (this_coro::aborted()) {
            sem.release();
            throw coroutine_abort{};
        }

        restorer.state = 1;
        co_return guard(this);
    }

private:
    binary_semaphore sem{1};
};

// Non-void specialization wrapping a value and reusing mutex<void>
template<typename T>
class mutex {
public:
    class guard {
    public:
        guard() = default;
        guard(mutex *self, typename mutex<void>::guard &&g)
                : self(self)
                , base_guard(std::move(g)) {}

        guard(guard &&) noexcept = default;
        guard &operator=(guard &&) noexcept = default;

        T &operator*() {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator*() called on nullptr");
            return self->value;
        }
        const T &operator*() const {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator*() called on nullptr");
            return self->value;
        }
        T *operator->() {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator->() called on nullptr");
            return &self->value;
        }
        const T *operator->() const {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator->() called on nullptr");
            return &self->value;
        }

    private:
        mutex *self{nullptr};
        typename mutex<void>::guard base_guard;
    };

    mutex() = default;
    mutex(const mutex &) = delete;
    mutex(mutex &&) = delete;

    explicit mutex(const T &val)
            : value{val} {}
    explicit mutex(T &&val)
            : value{std::move(val)} {}
    template<typename... Args>
    explicit mutex(Args &&...args)
            : value(std::forward<Args>(args)...) {}

    std::optional<guard> try_lock() {
        auto g = base.try_lock();
        if (!g)
            return std::nullopt;
        return guard(this, std::move(*g));
    }

    future_inline<guard> lock() {
        auto g = co_await base.lock();
        co_return guard(this, std::move(g));
    }

private:
    T value{};
    mutex<> base;
};

};  // namespace asco::sync

namespace asco {

using sync::mutex;

};

#endif
