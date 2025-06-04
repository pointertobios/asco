// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_MUTEX_H
#define ASCO_SYNC_MUTEX_H 1

#include <functional>

#include <asco/future.h>
#include <asco/sync/semaphore.h>

namespace asco::sync {

template<typename T>
class mutex {
public:
    struct guard {
        guard() = default;

        explicit guard(mutex *self)
                : self(self) {}

        guard(guard &&rhs) noexcept
                : self(rhs.self) {
            rhs.moved = true;
        }

        guard &operator=(guard &&rhs) noexcept {
            self = rhs.self;
            rhs.moved = true;
            return *this;
        }

        ~guard() {
            if (!self || moved)
                return;
            self->sem.release();
        }

        T &operator*() {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator*() called on nullptr");
            if (moved)
                throw asco::runtime_error("mutex::guard::operator*() called on moved");
            return self->value;
        }
        const T &operator*() const {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator*() called on nullptr");
            if (moved)
                throw asco::runtime_error("mutex::guard::operator*() called on moved");
            return self->value;
        }

        T *operator->() {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator->() called on nullptr");
            if (moved)
                throw asco::runtime_error("mutex::guard::operator->() called on moved");
            return &self->value;
        }
        const T *operator->() const {
            if (!self)
                throw asco::runtime_error("mutex::guard::operator->() called on nullptr");
            if (moved)
                throw asco::runtime_error("mutex::guard::operator->() called on moved");
            return &self->value;
        }

    private:
        mutex *self;
        bool moved{false};
    };

    mutex()
            : value{} {}

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

                if (state == 1)
                    self->sem.release();
            }
        } restorer{this};

        if (this_coro::aborted())
            co_return std::move(this_coro::aborted_value<guard>);

        co_await sem.acquire();

        if (this_coro::aborted()) {
            sem.release();
            co_return std::move(this_coro::aborted_value<guard>);
        }

        restorer.state = 1;
        co_return guard(this);
    }

private:
    T value;
    binary_semaphore sem{1};
};

};  // namespace asco::sync

namespace asco {

using sync::mutex;

};

#endif
