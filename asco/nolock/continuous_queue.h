// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_NOLOCK_CONTINUOUS_QUEUE_H
#define ASCO_NOLOCK_CONTINUOUS_QUEUE_H 1

#include <expected>
#include <limits>
#include <new>
#include <tuple>

#include <asco/core/slub.h>
#include <asco/rterror.h>
#include <asco/utils/concepts.h>
#include <asco/utils/concurrency.h>
#include <asco/utils/pubusing.h>

namespace asco::nolock::continuous_queue {

using namespace types;
using namespace concepts;

using concurrency::atomic_ptr;

// We assume that T's move constructor and move assignment operator is low-cost.
// In principle, they can't throw any exception.
// Exceptions throwing of T's constructor and move constructor are allowed during debugging.
template<move_secure T>
struct alignas(4096) frame {
    using element_type = monostate_if_void<T>;

public:
    struct slot {
        element_type val;
        alignas(alignof(element_type)) char _[0];
    };

    // So `index_nullopt` is always greater or equal to any size_t value
    constexpr static size_t index_nullopt = std::numeric_limits<size_t>::max();

    constexpr static bool passing_element_type_is_rvalue = !base_type<element_type>;

    // This is size_t::max() when receiver have not go to this frame.
    alignas(std::hardware_destructive_interference_size) atomic_size_t head{index_nullopt};

    // Set to size_t::max() when the sender went to next frame.
    alignas(std::hardware_destructive_interference_size) atomic_size_t tail{0};

    // Index of the next must be released element.
    alignas(std::hardware_destructive_interference_size) atomic_size_t released{0};

    // There is no ABA problem, because this pointer will only change from nullptr once and then never change.
    alignas(std::hardware_destructive_interference_size) atomic<frame *> next{nullptr};

    atomic_size_t refcount{1};

    // When sender stopped, iterate from current frame to set sender_stopped.
    atomic_bool sender_stopped{false};

    // When receiver stopped, iterate the full frame chain to set receiver_stopped.
    atomic_bool receiver_stopped{false};

    constexpr static size_t meta_size = std::hardware_destructive_interference_size * 3 + sizeof(next)
                                        + sizeof(refcount) + sizeof(sender_stopped)
                                        + sizeof(receiver_stopped);

    char padding
        [std::hardware_destructive_interference_size
         - (sizeof(next) + sizeof(refcount) + sizeof(sender_stopped) + sizeof(receiver_stopped))];

    constexpr static size_t header_size = meta_size + sizeof(padding);

    static_assert(
        (header_size % alignof(element_type)) == 0,
        "[ASCO] continuous_queue: Cannot fit alignment of element_type.");

    constexpr static size_t data_size = 4096 - header_size;

    constexpr static size_t group_size = std::hardware_destructive_interference_size;
    constexpr static bool grouped = sizeof(slot) <= group_size;
    constexpr static size_t group_count = data_size / group_size;

    constexpr static size_t length =
        grouped ? ((group_size / sizeof(slot)) * group_count) : (data_size / sizeof(slot));

    static_assert(
        length > 16, "[ASCO] Object is too large. There is less than/only 16 objects' room in one frame.");

    // There is no problem about alignment:
    // length > 16 means sizeof(T) always less than 256.
    // sizeof(T) < 256 means alignof(T) usually less than 256.
    // The header_size is usually 256, so the address of the stored elements always fit the alignment
    // requirement of T.
    char data[data_size];

    element_type &operator[](size_t index) {
        if (index >= length)
            throw runtime_error(
                "[ASCO] asco::nolock::continuous_queue::frame::operator[]: Index out of size.");

        if constexpr (grouped)
            return reinterpret_cast<slot *>(data + group_size * (index % group_count))[index / group_count]
                .val;
        else
            return reinterpret_cast<slot *>(data)[index].val;
    }

    static frame *create() noexcept { return new frame(); }

    void preset_head() noexcept { head.store(0, morder::release); }

    void link() noexcept { refcount.fetch_add(1, morder::relaxed); }

    void unlink() noexcept {
        if (refcount.fetch_sub(1, morder::acq_rel) <= 1) {
            // Recursive unlink is ok, because most sender and most receiver usually at the same frame.
            // There is hardly a deep recursing problem.
            if (auto next = this->next.load(morder::acquire); next)
                next->unlink();
            delete this;
        }
    }

private:
    frame() = default;

    void *operator new(std::size_t) noexcept {
        auto curr = freelist.load(morder::acquire);

        if (auto next = curr ? curr->next : nullptr; curr) {
            do {
                curr = freelist.load(morder::acquire);
                next = curr ? curr->next : nullptr;
            } while (!freelist.compare_exchange_weak(curr, next, morder::acq_rel, morder::acquire));

            if (curr)
                return curr->obj();
            else
                goto alloc;
        }

    alloc:
        return ::operator new(sizeof(frame), std::align_val_t{alignof(frame)});
    }

    void operator delete(void *ptr) noexcept {
        auto obj = core::slub::object<frame>::from(reinterpret_cast<frame *>(ptr));

        if (auto curr = freelist.load(morder::acquire); !curr || curr->len < freelist_max) {
            do {
                curr = freelist.load(morder::acquire);
                if (curr && curr->len >= freelist_max)
                    goto dealloc;
                obj->next = curr;
                obj->len = curr ? curr->len + 1 : 1;
            } while (!freelist.compare_exchange_weak(curr, obj, morder::acq_rel, morder::acquire));
            return;
        }

    dealloc:
        ::operator delete(ptr, std::align_val_t{alignof(frame)});
    }

    static atomic_ptr<core::slub::object<frame>> freelist;
    constexpr static size_t freelist_max = 8192;
};

static_assert(sizeof(frame<void>) == 4096);

template<move_secure T>
inline atomic_ptr<core::slub::object<frame<T>>> frame<T>::freelist{nullptr};

// Always thread-safe.
template<move_secure T>
class sender {
    using frame_type = frame<T>;
    using element_type = typename frame_type::element_type;
    using slot = typename frame_type::slot;
    constexpr static bool passing_element_type_is_rvalue = frame<T>::passing_element_type_is_rvalue;

public:
    sender() noexcept = default;

    sender(frame_type *f) noexcept
            : f{f} {
        if (f)
            f->link();
    }

    sender(const sender &rhs) noexcept
            : f{rhs.f} {
        if (f)
            f->link();
    }

    sender &operator=(const sender &rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~sender();
        new (this) sender(rhs);
        return *this;
    }

    sender(sender &&rhs) noexcept
            : f{rhs.f} {
        rhs.f = nullptr;
    }

    sender &operator=(sender &&rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~sender();
        new (this) sender(std::move(rhs));
        return *this;
    }

    ~sender() noexcept {
        if (!f)
            return;

        f->unlink();
        f = nullptr;
    }

    void stop() noexcept {
        for (auto fr = f; fr; fr = fr->next.load(morder::acquire)) {
            fr->sender_stopped.store(true, morder::release);
        }
        if (f) {
            f->unlink();
            f = nullptr;
        }
    }

    bool is_stopped() const noexcept {
        if (!f)
            return true;

        return f->sender_stopped.load(morder::acquire) || f->receiver_stopped.load(morder::acquire);
    }

    [[nodiscard]] std::optional<element_type> push(passing<element_type> val) {
        if (!f)
            throw runtime_error(
                "[ASCO] nolock::continuous_queue::sender::push(): sender didn't bind to a continuous_queue");

        size_t index;

        do {
            if (f->sender_stopped.load(morder::acquire) || f->receiver_stopped.load(morder::acquire)) {
                return std::move(val);
            }

            if (auto t = f->tail.load(morder::acquire); t != frame_type::index_nullopt) {
                do {
                    t = f->tail.load(morder::acquire);
                    if (t == frame_type::index_nullopt)
                        goto frame_full;
                } while (!f->tail.compare_exchange_weak(
                    t, t + 1 <= frame_type::length ? t + 1 : frame_type::index_nullopt, morder::acq_rel,
                    morder::relaxed));
                index = t;
            } else {
            frame_full:
                index = frame_type::index_nullopt;
            }

            if (index < frame_type::length)
                break;

            auto next = f->next.load(morder::acquire);
            if (!next) {
                auto newf = frame_type::create();
                if (!f->next.compare_exchange_strong(next, newf, morder::acq_rel, morder::acquire)) {
                    newf->unlink();
                } else {
                    next = newf;
                }
            }

            // Frame always create with refcount 1, so the current frame's next pointer handle
            // this 1.
            // So we have to link next only once.

            next->link();
            f->unlink();
            f = next;

        } while (true);

        if constexpr (passing_element_type_is_rvalue)
            new (&(*f)[index]) element_type{std::move(val)};
        else
            new (&(*f)[index]) element_type{val};

        // Serial releasing.
        // Current sender must wait for previous element to be released.
        // In this framework, this is low-cost.
        // Because all worker threads are binded to different CPU core.

        for (size_t i{0}; f->released.load(morder::acquire) != index; i++) {
            // We need low latency here.
            // At the same time, every push is very fast. We might hardly wait very long.
            // So just busy wait here when `i` <= 10000. I think `i` can hardly reach 10000.
            if (i > 10000)
                concurrency::cpu_relax();
        }
        f->released.store(index + 1, morder::release);

        return std::nullopt;
    }

private:
    frame_type *f{nullptr};
};

enum class pop_fail {
    non_object,  // No object available now.
    closed       // This queue is closed.
};

// Always thread-safe.
template<move_secure T>
class receiver {
    using frame_type = frame<T>;
    using element_type = typename frame_type::element_type;
    using slot = typename frame_type::slot;
    static constexpr bool passing_element_type_is_rvalue = frame<T>::passing_element_type_is_rvalue;

public:
    receiver() noexcept = default;

    receiver(frame_type *f) noexcept
            : f{f} {
        if (f) {
            f->link();
        }
    }

    receiver(const receiver &rhs) noexcept
            : f{rhs.f} {
        if (f)
            f->link();
    }

    receiver &operator=(const receiver &rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~receiver();
        new (this) receiver(rhs);
        return *this;
    }

    receiver(receiver &&rhs) noexcept
            : f{rhs.f} {
        rhs.f = nullptr;
    }

    receiver &operator=(receiver &&rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~receiver();
        new (this) receiver(std::move(rhs));
        return *this;
    }

    ~receiver() noexcept {
        if (!f)
            return;

        f->unlink();
        f = nullptr;
    }

    void stop() noexcept {
        for (auto fr = f; fr; fr = fr->next.load(morder::acquire)) {
            fr->receiver_stopped.store(true, morder::release);
        }
        if (f) {
            f->unlink();
            f = nullptr;
        }
    }

    bool is_stopped() const noexcept {
        if (!f)
            return true;

        return f->head.load(morder::acquire) >= f->released.load(morder::acquire)
               && (f->receiver_stopped.load(morder::acquire) || f->sender_stopped.load(morder::acquire));
    }

    [[nodiscard]] std::expected<element_type, pop_fail> pop() {
        if (!f)
            throw runtime_error(
                "[ASCO] nolock::continuous_queue::receiver::pop(): receiver didn't bind to a continuous_queue");

        size_t index;

        do {
            if (f->head.load(morder::acquire) >= f->released.load(morder::acquire)
                && (f->sender_stopped.load(morder::acquire) || f->receiver_stopped.load(morder::acquire))) {
                return std::unexpected(pop_fail::closed);
            }

            auto h = f->head.load(morder::acquire);

            do {
                h = f->head.load(morder::acquire);
                if (h < frame_type::length && h >= f->released.load(morder::acquire)) {
                    return std::unexpected(pop_fail::non_object);
                } else if (h >= frame_type::length) {
                    break;
                }
            } while (!f->head.compare_exchange_weak(
                h, h + 1 <= frame_type::length ? h + 1 : frame_type::index_nullopt, morder::acq_rel,
                morder::relaxed));

            if (h >= frame_type::length) {
                auto next = f->next.load(morder::acquire);
                size_t spin_count{0};

            next_judge:
                if (next) {
                    next->link();
                    f->unlink();
                    f = next;
                    auto n = frame_type::index_nullopt;
                    f->head.compare_exchange_strong(n, 0, morder::release, morder::relaxed);
                    continue;
                } else if (f->tail.load(morder::acquire) == h) {
                    return std::unexpected(pop_fail::non_object);
                } else if (h == frame_type::length) {
                    return std::unexpected(pop_fail::non_object);
                } else if (h == frame_type::index_nullopt) {
                    next = f->next.load(morder::acquire);

                    spin_count++;
                    if (spin_count >= 10000)
                        concurrency::cpu_relax();

                    goto next_judge;
                } else {
                    return std::unexpected(pop_fail::closed);
                }
            }

            index = h;

            break;
        } while (true);

        auto element = &(*f)[index];

        if constexpr (passing_element_type_is_rvalue) {
            auto res = std::move(*element);
            element->~element_type();
            return res;
        } else {
            auto res = *element;
            element->~element_type();
            return res;
        }
    }

private:
    frame_type *f{nullptr};
};

template<move_secure T>
std::tuple<sender<T>, receiver<T>> create() {
    using frame_type = frame<T>;
    auto f = frame_type::create();
    f->preset_head();
    auto res = std::tuple{sender<T>(f), receiver<T>(f)};
    f->unlink();
    return res;
}

};  // namespace asco::nolock::continuous_queue

namespace asco::continuous_queue {

using nolock::continuous_queue::create;
using nolock::continuous_queue::pop_fail;
using nolock::continuous_queue::receiver;
using nolock::continuous_queue::sender;

};  // namespace asco::continuous_queue

#endif
