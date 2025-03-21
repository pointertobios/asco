#ifndef ASCO_CHANNEL_H
#define ASCO_CHANNEL_H

#include <condition_variable>
#include <mutex>
#include <optional>
#include <tuple>

#include <asco/utils/utils.h>

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
    std::mutex mutex;
    std::condition_variable cv;
    bool waiting;
    bool ok{false};
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
    bool exited{false};
    std::mutex mutex;

public:
    bool shared{false};

public:
    sender(channel_frame<T> *initial_frame, const int frame_size, channel_state &state)
        : frame(initial_frame), frame_size(frame_size), state(state) {}
    
    sender(sender &&rhs)
        : frame(rhs.frame), frame_size(rhs.frame_size), state(rhs.state) {
        rhs.exited = true;
    }

    sender(const sender &rhs)
        : frame(rhs.frame), frame_size(rhs.frame_size), state(rhs.state) {
        *const_cast<bool *>(&rhs.exited) = true;
    }

    void operator=(sender &&rhs) {
        frame = rhs.frame;
        frame_size = rhs.frame_size;
        state = rhs.state;
        rhs.exited = true;
    }

    void send(T &&value) {
        if (exited)
            throw std::runtime_error("[ASCO] Channel error: Cannot send on a closed channel.");
        mutex.lock();
        if (*frame->sender_index == frame_size) {
            frame->sender_index = std::nullopt;
            frame->next = new channel_frame<T>(frame_size);
            frame = frame->next;
        }

        frame->data[(*frame->sender_index)++] = value;
        if (state->waiting) {
            state->ok = true;
            state->cv.notify_one();
        }
        mutex.unlock();
    }

    void send(T &value) {
        send(std::move(value));
    }

    void stop() {
        auto *pstate = state.get();
        state.reset();
        {
            std::lock_guard lk(pstate->mutex);
            pstate->ok = true;
            pstate->stopped = true;
            pstate->cv.notify_all();
        }
        exited = true;
    }
};

template<typename T>
requires is_move_secure_v<T>
class receiver {
private:
    channel_frame<T> *frame;
    int frame_size;
    channel_state state;
    bool moved{false};
    std::mutex mutex;

public:
    bool shared{false};

public:
    receiver(channel_frame<T> *initial_frame, const int frame_size, channel_state &state)
        : frame(initial_frame), frame_size(frame_size), state(state) {}
    
    receiver(receiver &&rhs)
        : frame(rhs.frame), frame_size(rhs.frame_size), state(rhs.state) {
        rhs.moved = true;
    }

    receiver(const receiver &rhs)
        : frame(rhs.frame), frame_size(rhs.frame_size), state(rhs.state) {
        *const_cast<bool *>(&rhs.moved) = true;
    }

    void operator=(receiver &&rhs) {
        frame = rhs.frame;
        frame_size = rhs.frame_size;
        state = rhs.state;
        moved = true;
    }

    bool is_stopped() const {
        return state->stopped;
    }

    // You must test if the channel stopped before try_recv()
    std::optional<T> try_recv() {
        if (moved)
            throw std::runtime_error("[ASCO] Channel error: Cannot receive on a moved channel.");
        
        if (state->stopped)
            return std::nullopt;
        
        mutex.lock();

        if (state->stopped) {
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
        
        if (state->stopped)
            return std::nullopt;
        
        mutex.lock();

        if (state->stopped) {
            if (frame) {
                delete frame;
                frame = nullptr;
            }
            mutex.unlock();
            return std::nullopt;
        }

        if (frame->sender_index
                && *frame->receiver_index == *frame->sender_index) {
            state->waiting = true;
            std::unique_lock lk(state->mutex);
            state->cv.wait(lk, [this]{return state->ok;});
            lk.unlock();
            state->ok = false;
        }
        if (state->stopped) {
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
    return std::make_shared<sender<T>>(tx);
}

template<typename T>
requires is_move_secure_v<T>
using shared_receiver = std::shared_ptr<receiver<T>>;

template<typename T>
requires is_move_secure_v<T>
shared_receiver<T> make_shared_receiver(receiver<T> &&rx) {
    return std::make_shared<receiver<T>>(rx);
}

};

#endif
