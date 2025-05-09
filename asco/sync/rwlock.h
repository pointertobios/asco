// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_RWLOCK_H
#define ASCO_SYNC_RWLOCK_H 1

#include <asco/sync/semaphore.h>
#include <forward_list>

namespace asco::sync {

template<typename T>
class rwlock {
public:
    struct write_guard {
        write_guard() = default;

        explicit write_guard(rwlock *self)
                : self(self) {}

        write_guard(write_guard &&rhs) noexcept
                : self(rhs.self) {
            rhs.moved = true;
        }

        write_guard &operator=(write_guard &&rhs) noexcept {
            self = rhs.self;
            rhs.moved = true;
            return *this;
        }

        ~write_guard() {
            if (!self || moved)
                return;
            self->write_sem.release();
        }

        T &operator*() {
            if (!self)
                throw std::runtime_error("rwlock::write_guard::operator*() called on nullptr");
            if (moved)
                throw std::runtime_error("rwlock::write_guard::operator->() called on moved");
            return self->value;
        }
        const T &operator*() const {
            if (!self)
                throw std::runtime_error("rwlock::write_guard::operator*() called on nullptr");
            if (moved)
                throw std::runtime_error("rwlock::write_guard::operator->() called on moved");
            return self->value;
        }

        T *operator->() {
            if (!self)
                throw std::runtime_error("rwlock::write_guard::operator->() called on nullptr");
            if (moved)
                throw std::runtime_error("rwlock::write_guard::operator->() called on moved");
            return &self->value;
        }
        const T *operator->() const {
            if (!self)
                throw std::runtime_error("rwlock::write_guard::operator->() called on nullptr");
            if (moved)
                throw std::runtime_error("rwlock::write_guard::operator->() called on moved");
            return &self->value;
        }

    private:
        rwlock *self;
        bool moved{false};
    };

    struct read_guard {
        read_guard() = default;

        explicit read_guard(rwlock *self)
                : self(self) {}

        read_guard(read_guard &&rhs) noexcept
                : self(rhs.self) {
            rhs.moved = true;
        }

        read_guard &operator=(read_guard &&rhs) noexcept {
            self = rhs.self;
            rhs.moved = true;
            return *this;
        }

        ~read_guard() {
            if (!self || moved)
                return;
            if (self->read_count.fetch_sub(1, morder::release) == 1) {
                self->write_sem.release();
            }
        }

        const T &operator*() const {
            if (!self)
                throw std::runtime_error("rwlock::read_guard::operator*() called on nullptr");
            if (moved)
                throw std::runtime_error("rwlock::read_guard::operator->() called on moved");
            return self->value;
        }

        const T *operator->() const {
            if (!self)
                throw std::runtime_error("rwlock::read_guard::operator->() called on nullptr");
            if (moved)
                throw std::runtime_error("rwlock::read_guard::operator->() called on moved");
            return &self->value;
        }

    private:
        rwlock *self;
        bool moved{false};
    };

    rwlock() = default;

    explicit rwlock(const T &rhs)
            : value(rhs) {}

    explicit rwlock(T &&rhs)
            : value(std::move(rhs)) {}

    template<typename... Args>
    explicit rwlock(Args &&...args)
            : value(std::forward<Args>(args)...) {}

    future_inline<write_guard> write() {
        struct re {
            rwlock *self;
            int state{0};

            ~re() {
                if (!this_coro::aborted())
                    return;

                if (state == 1)
                    self->write_sem.release();
            }
        } restorer{this};

        if (this_coro::aborted()) {
            co_return std::move(this_coro::aborted_value<write_guard>);
        }

        co_await write_sem.acquire();

        if (this_coro::aborted()) {
            write_sem.release();
            co_return std::move(this_coro::aborted_value<write_guard>);
        }

        restorer.state = 1;
        co_return write_guard(this);
    }

    future_inline<read_guard> read() {
        struct re {
            rwlock *self;
            int state{0};

            ~re() {
                if (!this_coro::aborted())
                    return;

                if (state == 0)
                    return;
                if (self->read_count.fetch_sub(1, morder::release) > 1)
                    return;
                self->write_sem.release();
            }
        } restorer{this};

        if (this_coro::aborted()) {
            co_return std::move(this_coro::aborted_value<read_guard>);
        }

        if (read_count.fetch_add(1, morder::acquire) == 0)
            co_await write_sem.acquire();
        else if (!write_sem.try_acquire() && read_count.load(morder::acquire) == 1)
            co_await write_sem.acquire();

        if (this_coro::aborted()) {
            if (read_count.fetch_sub(1, morder::release) == 1)
                write_sem.release();

            co_return std::move(this_coro::aborted_value<read_guard>);
        }

        restorer.state = 1;
        co_return read_guard(this);
    }

private:
    T value;
    binary_semaphore write_sem{1};
    atomic_size_t read_count{0};
};

};  // namespace asco::sync

namespace asco {

template<typename T>
using rwlock = sync::rwlock<T>;

};  // namespace asco

#endif
