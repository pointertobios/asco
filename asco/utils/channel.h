#ifndef ASCO_CHANNEL_H
#define ASCO_CHANNEL_H

#include <condition_variable>
#include <mutex>
#include <optional>
#include <semaphore>
#include <tuple>

#include <asco/utils/utils.h>

using namespace asco;

namespace asco_inner {

template<typename T>
requires is_move_secure_v<T>
struct channel_frame {
    const int frame_size;
    // Initialized by 0,
    // because only the sender wants to send objects this channel_frame will be allocated.
    // If the sender went to next frame, this value will be std::nullopt.
    std::optional<int> sender_index{0};
    // Initialized by std::nullopt,
    // because the receiver may receiving in the previous frame.
    std::optional<int> receiver_index{std::nullopt};
    T *data{new T[frame_size]};
    channel_frame *next{nullptr};

    ~channel_frame() {
        if (data) {
            delete[] data;
            data = nullptr;
        }
    }
};

struct __channel_state {
    std::binary_semaphore sem{0};
    bool stopped{false};
};

using channel_state = std::shared_ptr<__channel_state>;

template<typename T>
requires is_move_secure_v<T>
class sender {
private:
    channel_frame<T> *frame;
    int frame_size;
    channel_state state;
    mutable bool exited{false};
    std::mutex mutex;

public:
    bool none{false};
    bool shared{false};

public:
    sender(): none{true} {}

    sender(channel_frame<T> *initial_frame, const int frame_size, channel_state &state)
        : frame(initial_frame), frame_size(frame_size), state(state) {}

    sender(sender &&rhs)
        : frame(rhs.frame), frame_size(rhs.frame_size), state(rhs.state) {
        rhs.exited = true;
    }

    sender(const sender &rhs) = delete;

    sender(sender &rhs) = delete;

    ~sender() {
        if (!none)
            stop();
    }

    void operator=(sender &&rhs) {
        if (!none)
            stop();
        frame = rhs.frame;
        frame_size = rhs.frame_size;
        state = rhs.state;
        rhs.exited = true;
        none = false;
    }

    void send(T &&value) {
        if (exited || none)
            throw std::runtime_error("[ASCO] Channel error: Cannot send on a closed channel.");
        mutex.lock();
        if (*frame->sender_index == frame_size) {
            frame->next = new channel_frame<T>(frame_size);
            auto *p = frame;
            frame = frame->next;
            p->sender_index = std::nullopt;
        }

        frame->data[(*frame->sender_index)++] = value;
        state->sem.release();
        mutex.unlock();
    }

    void send(T &value) {
        send(std::move(value));
    }

    void stop() {
        if (none || exited)
            return;

        auto *pstate = state.get();
        state.reset();
        {
            pstate->sem.release();
            pstate->stopped = true;
        }
        exited = true;
        none = true;
    }
};

template<typename T>
requires is_move_secure_v<T>
class receiver {
private:
    channel_frame<T> *frame;
    int frame_size;
    channel_state state;
    mutable bool moved{false};
    std::mutex mutex;

public:
    bool none;

public:
    receiver(): none{true} {}

    receiver(channel_frame<T> *initial_frame, const int frame_size, channel_state &state)
        : frame(initial_frame), frame_size(frame_size), state(state) {}
    
    receiver(receiver &&rhs)
        : frame(rhs.frame), frame_size(rhs.frame_size), state(rhs.state) {
        rhs.moved = true;
    }

    receiver(const receiver &rhs) = delete;

    receiver(receiver &rhs) = delete;

    void operator=(receiver &&rhs) {
        frame = rhs.frame;
        frame_size = rhs.frame_size;
        state = rhs.state;
        rhs.moved = true;
        none = false;
    }

    bool is_stopped() const {
        if (moved)
            throw std::runtime_error("[ASCO] Channel error: Moved channel.");
        if (!frame)
            return true;
        return state->stopped
                && frame->sender_index && *frame->sender_index == *frame->receiver_index;
    }

    std::optional<T> try_recv() {
        if (moved)
            throw std::runtime_error("[ASCO] Channel error: Cannot receive on a moved channel.");
        
        if (is_stopped())
            return std::nullopt;
        
        if (!mutex.try_lock())
            return std::nullopt;

        if (is_stopped()) {
            if (frame) {
                delete frame;
                frame = nullptr;
            }
            mutex.unlock();
            return std::nullopt;
        }

        if (frame->sender_index
                && *frame->receiver_index == *frame->sender_index) {
            mutex.unlock();
            return std::nullopt;
        }

        if (*frame->receiver_index == frame_size) {
            if (frame->sender_index)
                throw std::runtime_error("[ASCO] Channel inner error: The sender went to next frame but sender_index is not std::nullopt.");
            if (frame->next == nullptr)
                throw std::runtime_error("[ASCO] Channel inner error: The sender went to next frame but next frame is nullptr.");
            auto *p = frame;
            frame = frame->next;
            delete p;
            frame->receiver_index = 0;
        }

        auto &&res = std::move(frame->data[(*frame->receiver_index)++]);
        mutex.unlock();
        return res;
    }

    std::optional<T> recv() {
        if (moved)
            throw std::runtime_error("[ASCO] Channel error: Cannot receive on a moved channel.");
        
        if (is_stopped())
            return std::nullopt;
        
        mutex.lock();

        if (is_stopped()) {
            if (frame) {
                delete frame;
                frame = nullptr;
            }
            mutex.unlock();
            return std::nullopt;
        }

        if (frame->sender_index
                && *frame->receiver_index == *frame->sender_index) {
            state->sem.acquire();
        }
        if (is_stopped()) {
            if (frame) {
                delete frame;
                frame = nullptr;
            }
            mutex.unlock();
            return std::nullopt;
        }

        if (*frame->receiver_index == frame_size) {
            if (frame->sender_index)
                throw std::runtime_error("[ASCO] Channel inner error: The sender went to next frame but sender_index is not std::nullopt.");
            if (frame->next == nullptr)
                throw std::runtime_error("[ASCO] Channel inner error: The sender went to next frame but next frame is nullptr.");
            auto *p = frame;
            frame = frame->next;
            delete p;
            frame->receiver_index = 0;
        }

        auto &&res = std::move(frame->data[(*frame->receiver_index)++]);
        mutex.unlock();
        return res;
    }
};

template<typename T>
requires is_move_secure_v<T>
std::tuple<sender<T>, receiver<T>> channel(int frame_size) {
    channel_frame<T> *frame = new channel_frame<T>(frame_size);
    frame->receiver_index = 0;
    auto state_tx = std::make_shared<__channel_state>();
    auto state_rx = state_tx;
    return std::make_tuple(
        sender<T>(frame, frame_size, state_tx),
        receiver<T>(frame, frame_size, state_rx)
    );
}

template<typename T>
requires is_move_secure_v<T>
using shared_sender = std::shared_ptr<sender<T>>;

template<typename T>
requires is_move_secure_v<T>
shared_sender<T> make_shared_sender(sender<T> &&tx) {
    return std::make_shared<sender<T>>(std::move(tx));
}

template<typename T>
requires is_move_secure_v<T>
using shared_receiver = std::shared_ptr<receiver<T>>;

template<typename T>
requires is_move_secure_v<T>
shared_receiver<T> make_shared_receiver(receiver<T> &&rx) {
    return std::make_shared<receiver<T>>(std::move(rx));
}

};

#endif
