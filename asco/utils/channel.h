// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_CHANNEL_H
#define ASCO_UTILS_CHANNEL_H

#include <condition_variable>
#include <expected>
#include <new>
#include <optional>
#include <semaphore>
#include <tuple>
#include <utility>

#include <asco/rterror.h>
#include <asco/utils/concepts.h>
#include <asco/utils/mutex.h>
#include <asco/utils/pubusing.h>

namespace asco::inner {

using namespace asco::types;

template<typename T, size_t Size>
struct channel_frame {
    std::byte buffer[Size * sizeof(T)];

    // Start with index 0, and maybe can goto next frame.
    // When sender port went to next frame, set this to std::nullopt.
    std::optional<size_t> sender{0};

    // Start with std::nullopt, because the receiver may stop at
    // the previous frame.
    std::optional<size_t> receiver{std::nullopt};

    // The more 1 count is to support `sender went to next frame` signal.
    // If sender acquired this sem but `sender` index is Size,
    // then sender can to go next frame.
    std::counting_semaphore<Size + 1> sem{0};

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
            throw asco::runtime_error("[ASCO] sender::sender(): Cannot move from a NONE sender object.");
        rhs.moved = true;
    }

    sender(sender &&rhs)
            : frame{rhs.frame} {
        if (rhs.none)
            throw asco::runtime_error("[ASCO] sender::sender(): Cannot move from a NONE sender object.");
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
    // If channel closed with unexpected case, throw a asco::runtime_error.
    std::optional<T> send(T &&data) {
        if (none)
            throw asco::runtime_error("[ASCO] sender::send(): Cannot do any action on a NONE sender object.");
        if (moved)
            throw asco::runtime_error("[ASCO] sender::send(): Cannot do any action after sender moved.");

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

        new (&((T *)frame->buffer)[(*frame->sender)++]) T{std::move(data)};
        frame->sem.release();

        return std::nullopt;
    }

    std::optional<T> send(T &data) { return send(std::move(data)); }

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
            throw asco::runtime_error(
                "[ASCO] receiver::receiver(): Cannot move from a NONE receiver object.");
        rhs.moved = true;
    }

    receiver(receiver &&rhs)
            : frame{rhs.frame} {
        if (rhs.none)
            throw asco::runtime_error(
                "[ASCO] receiver::receiver(): Cannot move from a NONE receiver object.");
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
            throw asco::runtime_error(
                "[ASCO] receiver::try_recv(): Cannot do any action on a NONE receiver object.");
        if (moved)
            throw asco::runtime_error(
                "[ASCO] receiver::try_recv(): Cannot do any action after receiver moved.");

        if (!frame->sem.try_acquire())
            return std::unexpected(receive_fail::non_object);

        if (frame->sender.has_value()) {
            if (is_stopped())
                return std::unexpected(receive_fail::closed);

            if (*frame->sender == *frame->receiver)
                throw asco::runtime_error(
                    "[ASCO] receiver::try_recv(): Sender game a new object, but sender index equals to receiver index.");

        } else if (*frame->receiver == FrameSize) {
            auto *f = frame;
            if (!f->next)
                throw asco::runtime_error(
                    "[ASCO] receiver::recv(): Sender went to next frame, but next frame is nullptr.");
            frame = f->next;
            delete f;
            frame->receiver = 0;

            if (!frame->sem.try_acquire())
                return std::unexpected(receive_fail::non_object);

            if (is_stopped())
                return std::unexpected(receive_fail::closed);

            if (frame->sender && *frame->sender == *frame->receiver)
                throw asco::runtime_error(
                    "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");
        }

        return std::move(((T *)frame->buffer)[(*frame->receiver)++]);
    }

    // If receive successful, return T value.
    // If channel closed, return std::nullopt.
    // If channel closed with unexpected case, throw a asco::runtime_error.
    // recv() if a coroutine, if aborted, return std::nullopt, but caller should
    // ignore the return value.
    std::optional<T> recv() {
        if (none)
            throw asco::runtime_error(
                "[ASCO] receiver::recv(): Cannot do any action on a NONE receiver object.");
        if (moved)
            throw asco::runtime_error("[ASCO] receiver::recv(): Cannot do any action after receiver moved.");

        frame->sem.acquire();

        if (frame->sender.has_value()) {
            if (is_stopped())
                return std::nullopt;

            if (*frame->sender == *frame->receiver)
                throw asco::runtime_error(
                    "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");

        } else if (*frame->receiver == FrameSize) {
            // go to next frame.
            auto *f = frame;
            if (!f->next)
                throw asco::runtime_error(
                    "[ASCO] receiver::recv(): Sender went to next frame, but next frame is nullptr.");
            frame = f->next;
            delete f;
            frame->receiver = 0;

            frame->sem.acquire();

            if (is_stopped())
                return std::nullopt;

            if (frame->sender && *frame->sender == *frame->receiver)
                throw asco::runtime_error(
                    "[ASCO] receiver::recv(): Sender gave a new object, but sender index equals to receiver index.");
        }

        return std::move(((T *)frame->buffer)[(*frame->receiver)++]);
    }

private:
    channel_frame<T, FrameSize> *frame;

    bool mutable moved{false};
    bool none{false};
};

namespace ss {

template<move_secure T, size_t FrameSize = 1024>
std::tuple<sender<T, FrameSize>, receiver<T, FrameSize>> channel() {
    auto *frame = new channel_frame<T, FrameSize>();
    frame->receiver = 0;
    return {sender<T, FrameSize>(frame), receiver<T, FrameSize>(frame)};
}

};  // namespace ss

namespace ms {

template<typename T, size_t FrameSize = 1024>
struct mpsc_channel {
    std::vector<inner::receiver<T, FrameSize>> receivers;
    std::binary_semaphore awaker{0};
};

template<typename T, size_t FrameSize = 1024>
class receiver {
public:
    receiver()
            : _channel(nullptr) {}

    receiver(std::shared_ptr<mutex<mpsc_channel<T, FrameSize>>> &_channel)
            : _channel(_channel) {}

    bool is_stopped() {
        with(auto guard = _channel->lock()) {
            for (auto &rx : guard->receivers) {
                if (rx.is_stopped())
                    return true;
            }
        }
        return false;
    }

    std::expected<T, typename inner::receiver<T, FrameSize>::receive_fail> try_recv() {
        do {
            with(auto guard = _channel->lock()) {
                if (guard->receivers.empty())
                    asco::runtime_error("[ASCO] ms::receiver::try_recv(): mpsc receiver cannot be empty.");

                for (auto &rx : guard->receivers) {
                    auto res = rx.try_recv();
                    if (res)
                        return res;
                    else if (res.error() == inner::receiver<T, FrameSize>::receive_fail::closed)
                        return std::unexpected{inner::receiver<T, FrameSize>::receive_fail::closed};
                }
            }
        } while (({
            auto g = _channel->lock();
            bool b = false;
            if (g->awaker.try_acquire()) {
                b = true;
            }
            b;
        }));

        return std::unexpected{inner::receiver<T, FrameSize>::receive_fail::non_object};
    }

    auto recv() {
        std::binary_semaphore *s;

        do {
            with(auto guard = _channel->lock()) {
                assert(!guard->receivers.empty());

                for (auto &rx : guard->receivers) {
                    auto res = rx.try_recv();
                    if (res)
                        return *res;
                    else if (res.error() == inner::receiver<T, FrameSize>::receive_fail::closed)
                        return std::nullopt;
                }

                s = &guard->awaker;
            }
        } while ((s->acquire(), true));

        std::unreachable();
    }

private:
    std::shared_ptr<mutex<mpsc_channel<T, FrameSize>>> _channel;
};

template<typename T, size_t FrameSize = 1024>
class sender {
public:
    sender(inner::sender<T, FrameSize> &&_sender, std::shared_ptr<mutex<mpsc_channel<T, FrameSize>>> &channel)
            : _sender(std::move(_sender))
            , channel(channel) {}

    sender(const sender &rhs)
            : channel(rhs.channel) {
        auto [tx, rx] = ss::channel<T, FrameSize>();
        _sender = std::move(tx);
        with(auto guard = channel->lock()) {
            guard->receivers.emplace_back(std::move(rx));
            guard->awaker.release();
        }
    }

    sender(sender &&rhs)
            : _sender(std::move(rhs._sender))
            , channel(std::move(rhs.channel)) {}

    sender &operator=(sender &&rhs) {
        _sender = std::move(rhs._sender);
        channel = std::move(rhs.channel);
        return *this;
    }

    ~sender() {
        if (channel)
            stop();
    }

    void stop() { _sender.stop(); }

    auto send(T &&data) { return _sender.send(std::move(data)); }

    auto send(T &data) { return _sender.send(std::move(data)); }

private:
    inner::sender<T, FrameSize> _sender;
    std::shared_ptr<mutex<mpsc_channel<T, FrameSize>>> channel;
};

template<typename T, size_t FrameSize = 1024>
std::tuple<sender<T, FrameSize>, receiver<T, FrameSize>> channel() {
    auto [tx, rx] = ss::channel<T, FrameSize>();
    auto mpsc = std::make_shared<mutex<mpsc_channel<T, FrameSize>>>();
    mpsc->lock()->receivers.emplace_back(std::move(rx));
    return std::make_tuple(
        std::move(sender<T, FrameSize>(std::move(tx), mpsc)), std::move(receiver<T, FrameSize>(mpsc)));
}

};  // namespace ms

};  // namespace asco::inner

#endif
