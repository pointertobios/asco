#ifndef ASCO_UTILS_MUTEX_H
#define ASCO_UTILS_MUTEX_H 1

#include <atomic>
#include <mutex>
#include <optional>

namespace asco_inner {

template<typename T>
class rwmutex {
private:
    T inner;
    std::atomic_uint64_t reader_count{0};
    std::mutex read_m;
    std::mutex m;

    bool moved{false};

public:
    rwmutex(T &&t) : inner(std::move(t)) {}
    rwmutex(T &t) : inner(t) {}

    class guard {
    public:
        explicit guard(rwmutex &m, bool try_ = false)
            : m(m) {
            if (try_) {
                if (!m.m.try_lock()) {
                    failed = true;
                    return;
                }
            } else {
                m.m.lock();
            }
        }

        guard(guard &&other)
            : m(other.m) {
            other.moved = true;
        }

        ~guard() {
            if (moved)
                return;
            m.m.unlock();
        }

        operator bool() const { return !failed; }

        T *operator->() { return &m.inner; }
        T &operator*() { return m.inner; }

        const T *operator->() const { return &m.inner; }
        const T &operator*() const { return m.inner; }

    private:
        rwmutex &m;
        bool failed{false};

        bool moved{false};
    };

    guard write() {
        return std::move(guard(*this));
    }

    std::optional<guard> try_write() {
        if (auto res = guard(*this, true); res) {
            return std::move(res);
        } else {
            return std::nullopt;
        }
    }

    class const_guard {
    public:
        explicit const_guard(rwmutex &m, bool try_ = false)
            : m(m) {
            if (try_) {
                if (!m.m.try_lock()) {
                    failed = true;
                    return;
                }
            } else {
                m.m.lock();
            }
            if (!m.reader_count)
                m.read_m.lock();
            m.reader_count++;
        }

        const_guard(const_guard &&other)
            : m(other.m) {
            other.moved = true;
        }

        ~const_guard() {
            if (moved)
                return;
            m.m.unlock();
            m.reader_count--;
            if (!m.reader_count)
                m.read_m.unlock();
        }

        operator bool() const { return !failed; }

        const T *operator->() const { return &m.inner; }
        const T &operator*() const { return m.inner; }

    private:
        rwmutex &m;
        bool failed{false};

        bool moved{false};
    };

    const_guard read() {
        return std::move(const_guard(*this));
    }

    std::optional<const_guard> try_read() {
        if (auto res = const_guard(*this, true); res) {
            return std::move(res);
        } else {
            return std::nullopt;
        }
    }

};

};

#endif
