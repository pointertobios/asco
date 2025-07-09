#ifndef ASCO_UTILS_MUTEX_H
#define ASCO_UTILS_MUTEX_H 1

#include <semaphore>

#include <asco/rterror.h>

namespace asco::inner {

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

    guard lock() {
        sem.acquire();
        return guard(this);
    }

private:
    T value;
    std::binary_semaphore sem{1};
};

};  // namespace asco::inner

#endif
