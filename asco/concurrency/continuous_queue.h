// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

// This is a lock-free MPMC queue.

#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <tuple>

#include <asco/concurrency/concurrency.h>
#include <asco/concurrency/queue.h>
#include <asco/core/pool_allocator.h>
#include <asco/panic/panic.h>
#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco::concurrency::continuous_queue {

using namespace types;
using namespace concepts;

using queue::pop_fail;

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

    // Next frame pointer publish once. There is no ABA.
    alignas(std::hardware_destructive_interference_size)
#ifdef __cpp_lib_atomic_shared_ptr
        atomic<std::shared_ptr<frame>> next{};
#else
        std::shared_ptr<frame> next{};
#endif

    // When sender stopped, iterate from current frame to set sender_stopped.
    atomic_bool sender_stopped{false};

    // When receiver stopped, iterate the full frame chain to set receiver_stopped.
    atomic_bool receiver_stopped{false};

    constexpr static size_t meta_size = std::hardware_destructive_interference_size * 3 + sizeof(next)
                                        + sizeof(sender_stopped) + sizeof(receiver_stopped);

    char padding
        [std::hardware_destructive_interference_size
         - (sizeof(next) + sizeof(sender_stopped) + sizeof(receiver_stopped))];

    constexpr static size_t header_size = meta_size + sizeof(padding);

    static_assert(
        (header_size % alignof(element_type)) == 0,
        "[ASCO] continuous_queue: Cannot fit alignment of element_type.");

    constexpr static size_t data_size = 4096 - header_size;

    constexpr static size_t group_size = std::hardware_destructive_interference_size;
    constexpr static bool grouped = sizeof(slot) <= group_size / 2;
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

    explicit frame() noexcept = default;

    element_type &operator[](size_t index) {
        if (index >= length) {
            panic::panic("[ASCO] asco::nolock::continuous_queue::frame::operator[]: Index out of size.");
        }

        if constexpr (grouped)
            return reinterpret_cast<slot *>(data + group_size * (index % group_count))[index / group_count]
                .val;
        else
            return reinterpret_cast<slot *>(data)[index].val;
    }

    // Factory using std::allocate_shared and provided PMR resource for control block + object.
    static std::shared_ptr<frame> create() noexcept {
        try {
            return std::allocate_shared<frame>(core::mm::allocator<frame>(core::mm::default_pool<frame>()));
        } catch (...) { return {}; }
    }

    void preset_head() noexcept { head.store(0, morder::release); }
};

static_assert(sizeof(frame<void>) == 4096);

// Always thread-safe.
template<move_secure T>
class sender {
    using frame_type = frame<T>;
    using element_type = typename frame_type::element_type;
    using slot = typename frame_type::slot;
    constexpr static bool passing_element_type_is_rvalue = frame<T>::passing_element_type_is_rvalue;

public:
    sender() noexcept = default;

    sender(std::shared_ptr<frame_type> f) noexcept
            : f{std::move(f)} {}

    sender(const sender &rhs) noexcept
            : f{rhs.f} {}

    sender &operator=(const sender &rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~sender();
        new (this) sender(rhs);
        return *this;
    }

    sender(sender &&rhs) noexcept
            : f{std::move(rhs.f)} {}

    sender &operator=(sender &&rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~sender();
        new (this) sender(std::move(rhs));
        return *this;
    }

    ~sender() noexcept = default;

    void stop() noexcept {
        auto cur = f;
        f.reset();
        while (cur) {
            cur->sender_stopped.store(true, morder::release);
#ifdef __cpp_lib_atomic_shared_ptr
            cur = cur->next.load(morder::acquire);
#else
            cur = std::atomic_load_explicit(&cur->next, morder::acquire);
#endif
        }
    }

    bool is_stopped() const noexcept {
        if (!f)
            return true;

        return f->sender_stopped.load(morder::acquire) || f->receiver_stopped.load(morder::acquire);
    }

    [[nodiscard]] std::optional<element_type> push(passing<element_type> val) {
        if (!f) {
            panic::co_panic(
                "[ASCO] nolock::continuous_queue::sender::push(): sender didn't bind to a continuous_queue");
        }

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

#ifdef __cpp_lib_atomic_shared_ptr
            auto next = f->next.load(morder::acquire);
#else
            auto next = std::atomic_load_explicit(&f->next, morder::acquire);
#endif

            if (!next) {
                auto newf = frame_type::create();
                std::shared_ptr<frame_type> expected{};
#ifdef __cpp_lib_atomic_shared_ptr
                if (f->next.compare_exchange_strong(expected, newf, morder::acq_rel, morder::acquire)) {
                    next = std::move(newf);
                }
                if (!next)
                    next = f->next.load(morder::acquire);
#else
                if (std::atomic_compare_exchange_strong_explicit(
                        &f->next, &expected, newf, morder::acq_rel, morder::acquire)) {
                    next = std::move(newf);
                }
                if (!next)
                    next = std::atomic_load_explicit(&f->next, morder::acquire);
#endif
            }

            f = std::move(next);

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
    std::shared_ptr<frame_type> f{};
};

static_assert(queue::sender<sender<char>, char>);

// Always thread-safe.
template<move_secure T>
class receiver {
    using frame_type = frame<T>;
    using element_type = typename frame_type::element_type;
    using slot = typename frame_type::slot;
    static constexpr bool passing_element_type_is_rvalue = frame<T>::passing_element_type_is_rvalue;

public:
    receiver() noexcept = default;

    receiver(std::shared_ptr<frame_type> f) noexcept
            : f{std::move(f)} {}

    receiver(const receiver &rhs) noexcept
            : f{rhs.f} {}

    receiver &operator=(const receiver &rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~receiver();
        new (this) receiver(rhs);
        return *this;
    }

    receiver(receiver &&rhs) noexcept
            : f{std::move(rhs.f)} {}

    receiver &operator=(receiver &&rhs) noexcept {
        if (this == &rhs)
            return *this;

        this->~receiver();
        new (this) receiver(std::move(rhs));
        return *this;
    }

    ~receiver() noexcept = default;

    void stop() noexcept {
        auto cur = f;
        while (cur) {
            cur->receiver_stopped.store(true, morder::release);
#ifdef __cpp_lib_atomic_shared_ptr
            cur = cur->next.load(morder::acquire);
#else
            cur = std::atomic_load_explicit(&cur->next, morder::acquire);
#endif
        }
        f.reset();
    }

    bool is_stopped() const noexcept {
        if (!f)
            return true;

        return f->head.load(morder::acquire) >= f->released.load(morder::acquire)
               && (f->receiver_stopped.load(morder::acquire) || f->sender_stopped.load(morder::acquire));
    }

    [[nodiscard]] std::expected<element_type, pop_fail> pop() {
        if (!f) {
            panic::co_panic(
                "[ASCO] nolock::continuous_queue::receiver::pop(): receiver didn't bind to a continuous_queue");
        }

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
#ifdef __cpp_lib_atomic_shared_ptr
                auto next = f->next.load(morder::acquire);
#else
                auto next = std::atomic_load_explicit(&f->next, morder::acquire);
#endif
                size_t spin_count{0};

            next_judge:
                if (next) {
                    f = std::move(next);
                    auto n = frame_type::index_nullopt;
                    f->head.compare_exchange_strong(n, 0, morder::release, morder::relaxed);
                    continue;
                } else if (f->tail.load(morder::acquire) == h) {
                    return std::unexpected(pop_fail::non_object);
                } else if (h == frame_type::length) {
                    return std::unexpected(pop_fail::non_object);
                } else if (h == frame_type::index_nullopt) {
#ifdef __cpp_lib_atomic_shared_ptr
                    next = f->next.load(morder::acquire);
#else
                    next = std::atomic_load_explicit(&f->next, morder::acquire);
#endif

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
    std::shared_ptr<frame_type> f{};
};

static_assert(queue::receiver<receiver<char>, char>);

template<move_secure T>
struct create_s {
    using Sender = sender<T>;
    using Receiver = receiver<T>;

    std::tuple<Sender, Receiver> operator()() const {
        using frame_type = frame<T>;
        auto f = frame_type::create();
        if (!f)
            throw std::bad_alloc();
        f->preset_head();
        return std::tuple{sender<T>(f), receiver<T>(f)};
    }
};

static_assert(queue::creator<create_s<char>, char>);

template<move_secure T>
create_s<T> create;

};  // namespace asco::concurrency::continuous_queue

namespace asco::continuous_queue {

using concurrency::continuous_queue::create;
using concurrency::continuous_queue::receiver;
using concurrency::continuous_queue::sender;

};  // namespace asco::continuous_queue
