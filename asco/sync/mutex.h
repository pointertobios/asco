// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_MUTEX_H
#define ASCO_SYNC_MUTEX_H 1

#include <asco/future.h>
#include <asco/sync/semaphore.h>

namespace asco::sync {

template<typename T>
class mutex {
public:
    struct guard {
        guard() {}

        guard(mutex *self)
                : self(self) {}

        // Just for passing future_inline's movable check.
        guard(const guard &&rhs)
                : self(rhs.self) {
            rhs.moved = true;
        }

        // Just for passing future_inline's movable check.
        void operator=(const guard &&rhs) {
            self = rhs.self;
            rhs.moved = true;
        }

        ~guard() {
            if (self && !moved) {
                self->sem.release();
            }
        }

        T &operator*() {
            if (!self)
                throw std::runtime_error("mutex::guard::operator*() called on nullptr");
            if (moved)
                throw std::runtime_error("mutex::guard::operator*() called on moved");
            return self->value;
        }
        const T &operator*() const {
            if (!self)
                throw std::runtime_error("mutex::guard::operator*() called on nullptr");
            if (moved)
                throw std::runtime_error("mutex::guard::operator*() called on moved");
            return self->value;
        }

        T *operator->() {
            if (!self)
                throw std::runtime_error("mutex::guard::operator->() called on nullptr");
            if (moved)
                throw std::runtime_error("mutex::guard::operator->() called on moved");
            return &self->value;
        }
        const T *operator->() const {
            if (!self)
                throw std::runtime_error("mutex::guard::operator->() called on nullptr");
            if (moved)
                throw std::runtime_error("mutex::guard::operator->() called on moved");
            return &self->value;
        }

    private:
        mutex *self;
        bool mutable moved{false};
    };

    mutex()
            : value{} {}

    mutex(const T &val)
            : value{val} {}

    mutex(T &&val)
            : value{std::move(val)} {}

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

        if (this_coro::aborted()) {
            co_return std::move(this_coro::aborted_value<guard>);
        }

        co_await sem.acquire();

        if (this_coro::aborted()) {
            sem.release();
            co_return std::move(this_coro::aborted_value<guard>);
        }

        restorer.state = 1;
        co_return std::move(guard(this));
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
