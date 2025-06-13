// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_RWLOCK_H
#define ASCO_SYNC_RWLOCK_H 1

#include <forward_list>
#include <optional>

#include <asco/sync/semaphore.h>
#include <asco/utils/concepts.h>

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
                throw asco::runtime_error("rwlock::write_guard::operator*() called on nullptr");
            if (moved)
                throw asco::runtime_error("rwlock::write_guard::operator->() called on moved");
            return self->value;
        }
        const T &operator*() const {
            if (!self)
                throw asco::runtime_error("rwlock::write_guard::operator*() called on nullptr");
            if (moved)
                throw asco::runtime_error("rwlock::write_guard::operator->() called on moved");
            return self->value;
        }

        T *operator->() {
            if (!self)
                throw asco::runtime_error("rwlock::write_guard::operator->() called on nullptr");
            if (moved)
                throw asco::runtime_error("rwlock::write_guard::operator->() called on moved");
            return &self->value;
        }
        const T *operator->() const {
            if (!self)
                throw asco::runtime_error("rwlock::write_guard::operator->() called on nullptr");
            if (moved)
                throw asco::runtime_error("rwlock::write_guard::operator->() called on moved");
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
                throw asco::runtime_error("rwlock::read_guard::operator*() called on nullptr");
            if (moved)
                throw asco::runtime_error("rwlock::read_guard::operator->() called on moved");
            return self->value;
        }

        const T *operator->() const {
            if (!self)
                throw asco::runtime_error("rwlock::read_guard::operator->() called on nullptr");
            if (moved)
                throw asco::runtime_error("rwlock::read_guard::operator->() called on moved");
            return &self->value;
        }

    private:
        rwlock *self;
        bool moved{false};
    };

    rwlock() = default;

    rwlock(const rwlock &) = delete;
    rwlock(rwlock &&) = delete;

    explicit rwlock(const T &rhs)
            : value(rhs) {}

    explicit rwlock(T &&rhs)
            : value(std::move(rhs)) {}

    template<typename... Args>
    explicit rwlock(Args &&...args)
            : value(std::forward<Args>(args)...) {}

    std::optional<write_guard> try_write() {
        if (write_sem.try_acquire())
            return write_guard(this);
        return std::nullopt;
    }

    future_inline<write_guard> write() {
        struct re {
            rwlock *self;
            int state{0};

            ~re() {
                if (!this_coro::aborted())
                    return;

                this_coro::throw_coroutine_abort<future_inline<write_guard>>();

                if (state == 1)
                    self->write_sem.release();
            }
        } restorer{this};

        if (this_coro::aborted()) {
            throw coroutine_abort{};
        }

        co_await write_sem.acquire();

        if (this_coro::aborted()) {
            write_sem.release();
            throw coroutine_abort{};
        }

        restorer.state = 1;
        co_return write_guard(this);
    }

    std::optional<read_guard> try_read() {
        if (read_count.fetch_add(1, morder::acquire) == 0
            || (!write_sem.try_acquire() && read_count.load(morder::acquire) == 1)) {
            if (write_sem.try_acquire())
                return write_guard(this);
            else
                return std::nullopt;
        }
        return read_guard(this);
    }

    future_inline<read_guard> read() {
        struct re {
            rwlock *self;
            int state{0};

            ~re() {
                if (!this_coro::aborted())
                    return;

                this_coro::throw_coroutine_abort<future_inline<read_guard>>();

                if (state == 0)
                    return;
                if (self->read_count.fetch_sub(1, morder::release) > 1)
                    return;
                self->write_sem.release();
            }
        } restorer{this};

        if (this_coro::aborted()) {
            throw coroutine_abort{};
        }

        if (read_count.fetch_add(1, morder::acquire) == 0
            || (!write_sem.try_acquire() && read_count.load(morder::acquire) == 1))
            co_await write_sem.acquire();

        if (this_coro::aborted()) {
            if (read_count.fetch_sub(1, morder::release) == 1)
                write_sem.release();

            throw coroutine_abort{};
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
