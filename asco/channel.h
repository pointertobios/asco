// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_CHANNEL_H
#define ASCO_SYNC_CHANNEL_H 1

#include <expected>
#include <optional>
#include <tuple>
#include <vector>

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/pubusing.h>

namespace asco {

template<typename T, size_t Size>
struct channel_frame {
    T buffer[Size];

    // Start with index 0, and maybe can goto next frame.
    // When sender port went to next frame, set this to std::nullopt.
    std::optional<size_t> sender{0};

    // Start with std::nullopt, because the receiver may stop at
    // the previous frame.
    std::optional<size_t> receiver{std::nullopt};

    // The more 1 count is to support `sender went to next frame` signal.
    // If sender acquired this sem but `sender` index is Size,
    // then sender can to go next frame.
    semaphore<Size + 1> sem{0};

    // If true, receiver cannot go to next frame.
    atomic_bool stopped{false};

    // If true, sender cannot send any more object.
    atomic_bool receiver_lost{false};

    channel_frame *next{nullptr};
};

template<typename T, size_t FrameSize = 1024>
class sender {
public:
    sender()
            : none{true} {}

    sender(channel_frame<T, FrameSize> *start_frame)
            : frame{start_frame} {}

    sender(const sender &&rhs)
            : frame{rhs.frame} {
        if (rhs.none)
            throw std::runtime_error("[ASCO] sender::sender(): Cannot move from a NONE sender object.");
        rhs.moved = true;
    }

    sender(sender &&rhs)
            : frame{rhs.frame} {
        if (rhs.none)
            throw std::runtime_error("[ASCO] sender::sender(): Cannot move from a NONE sender object.");
        rhs.moved = true;
    }

    sender(const sender &) = delete;
    sender(sender &) = delete;

    ~sender() {
        if (none || moved)
            return;

        stop();
    }

    void operator=(const sender &&rhs) {
        stop();

        frame = rhs.frame;
        rhs.moved = true;

        none = false;
        moved = false;
    }

    void stop() {
        if (none || moved)
            return;

        frame->stopped.store(true, morder::seq_cst);
        frame->sem.release();
        none = true;
    }

    // If send successful, return std::nullopt.
    // When channel closed, return the passed data.
    // If channel closed with unexpected case, throw a std::runtime_error.
    [[nodiscard("[ASCO] sender::send(): You must deal with the case of channel closed.")]]
    std::optional<T> send(T &&data) {
        if (none)
            throw std::runtime_error("[ASCO] sender::send(): Cannot do any action on a NONE sender object.");
        if (moved)
            throw std::runtime_error("[ASCO] sender::send(): Cannot do any action after sender moved.");

        if (frame->receiver_lost.load(morder::seq_cst))
            return data;

        // This frame full, go to next frame.
        if (*frame->sender == FrameSize) {
            auto *f = frame;

            frame->sender = std::nullopt;

            frame->next = new channel_frame<T, FrameSize>;
            frame = frame->next;

            f->sem.release();
        }

        frame->buffer[(*frame->sender)++] = std::move(data);
        frame->sem.release();

        return std::nullopt;
    }

    [[nodiscard("[ASCO] sender::send(): You must deal with the case of channel closed.")]]
    __always_inline std::optional<T> send(T &data) {
        return send(std::move(data));
    }

private:
    channel_frame<T, FrameSize> *frame;

    bool mutable moved{false};
    bool none{false};
};

template<typename T, size_t FrameSize = 1024>
class receiver {
public:
    enum class receive_fail {
        non_object,
        closed,
    };

    receiver()
            : none{true} {}

    receiver(channel_frame<T, FrameSize> *start_frame)
            : frame{start_frame} {}

    receiver(const receiver &&rhs)
            : frame{rhs.frame} {
        if (rhs.none)
            throw std::runtime_error("[ASCO] receiver::receiver(): Cannot move from a NONE receiver object.");
        rhs.moved = true;
    }

    receiver(receiver &&rhs)
            : frame{rhs.frame} {
        if (rhs.none)
            throw std::runtime_error("[ASCO] receiver::receiver(): Cannot move from a NONE receiver object.");
        rhs.moved = true;
    }

    receiver(const receiver &) = delete;
    receiver(receiver &) = delete;

    ~receiver() {
        if (none || moved)
            return;

        stop();
    }

    void operator=(const receiver &&rhs) {
        stop();

        frame = rhs.frame;
        rhs.moved = true;

        none = false;
        moved = false;
    }

    void stop() {
        if (none || moved)
            return;

        auto *f = frame;
        while (f->next) f = f->next;
        f->receiver_lost.store(true, morder::seq_cst);
        none = true;
    }

    bool is_stopped() {
        if (none || moved)
            return true;

        if (frame->stopped.load(morder::seq_cst) && frame->sender && *frame->sender == *frame->receiver)
            return true;

        if (frame->receiver_lost.load(morder::relaxed))
            return true;

        return false;
    }

    std::expected<T, receive_fail> try_recv() {
        if (none)
            throw std::runtime_error(
                "[ASCO] receiver::try_recv(): Cannot do any action on a NONE receiver object.");
        if (moved)
            throw std::runtime_error(
                "[ASCO] receiver::try_recv(): Cannot do any action after receiver moved.");

        if (!frame->sem.try_acquire())
            return std::unexpected(receive_fail::non_object);

        if (frame->sender.has_value()) {
            if (is_stopped())
                return std::unexpected(receive_fail::closed);

            if (*frame->sender == *frame->receiver)
                throw std::runtime_error(
                    "[ASCO] receiver::try_recv(): Sender game a new object, but sender index equals to receiver index.");

        } else if (*frame->receiver == FrameSize) {
            auto *f = frame;
            if (!f->next)
                throw std::runtime_error(
                    "[ASCO] receiver::recv(): Sender went to next frame, but next frame is nullptr.");
            frame = f->next;
            delete f;
            frame->receiver = 0;

            if (!frame->sem.try_acquire())
                return std::unexpected(receive_fail::non_object);

            if (is_stopped())
                return std::unexpected(receive_fail::closed);

            if (frame->sender && *frame->sender == *frame->receiver)
                throw std::runtime_error(
                    "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");
        }

        return std::move(frame->buffer[(*frame->receiver)++]);
    }

    // If receive successful, return T value.
    // If channel closed, return std::nullopt.
    // If channel closed with unexpected case, throw a std::runtime_error.
    // recv() if a coroutine, if aborted, return std::nullopt, but caller should
    // ignore the return value.
    [[nodiscard("[ASCO] receiver::recv(): You must deal with the case of channel closed.")]]
    future_inline<std::optional<T>> recv() {
        struct re {
            receiver *self;
            int state{0};

            ~re() {
                if (!futures::aborted<future_inline<std::optional<T>>>())
                    return;

                switch (state) {
                case 2:
                    self->buffer.push_back(
                        futures::move_back_return_value<future_inline<std::optional<T>>, std::optional<T>>());
                case 1:
                    self->frame->sem.release();
                    break;
                default:
                    break;
                }
            }
        } restorer{this};

        if (none)
            throw std::runtime_error(
                "[ASCO] receiver::recv(): Cannot do any action on a NONE receiver object.");
        if (moved)
            throw std::runtime_error("[ASCO] receiver::recv(): Cannot do any action after receiver moved.");

        if (futures::aborted<future_inline<std::optional<T>>>()) {
            restorer.state = 0;
            co_return std::nullopt;
        }

        if (!buffer.empty()) {
            std::optional<T> res{std::move(buffer[0])};
            buffer.erase(buffer.begin());
            restorer.state = 2;
            co_return std::move(res);
        }

        co_await frame->sem.acquire();

        if (futures::aborted<future_inline<std::optional<T>>>()) {
            frame->sem.release();
            restorer.state = 0;
            co_return std::nullopt;
        }

        if (frame->sender.has_value()) {
            if (is_stopped()) {
                restorer.state = 1;
                co_return std::nullopt;
            }

            if (*frame->sender == *frame->receiver)
                throw std::runtime_error(
                    "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");

        } else if (*frame->receiver == FrameSize) {
            // go to next frame.
            auto *f = frame;
            if (!f->next)
                throw std::runtime_error(
                    "[ASCO] receiver::recv(): Sender went to next frame, but next frame is nullptr.");
            frame = f->next;
            delete f;
            frame->receiver = 0;

            co_await frame->sem.acquire();

            if (futures::aborted<future_inline<std::optional<T>>>()) {
                frame->sem.release();
                restorer.state = 0;
                co_return std::nullopt;
            }

            if (is_stopped()) {
                restorer.state = 1;
                co_return std::nullopt;
            }

            if (frame->sender && *frame->sender == *frame->receiver)
                throw std::runtime_error(
                    "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");
        }

        restorer.state = 2;
        co_return std::move(frame->buffer[(*frame->receiver)++]);
    }

private:
    channel_frame<T, FrameSize> *frame;
    std::vector<std::optional<T>> buffer;

    bool mutable moved{false};
    bool none{false};
};

namespace ss {

template<typename T, size_t FrameSize = 1024>
std::tuple<sender<T, FrameSize>, receiver<T, FrameSize>> channel() {
    auto *frame = new channel_frame<T, FrameSize>;
    frame->receiver = 0;
    return {sender<T, FrameSize>(frame), receiver<T, FrameSize>(frame)};
}

};  // namespace ss

};  // namespace asco

#endif
