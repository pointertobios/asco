#ifndef ASCO_FUTURE_NOCORO_H
#define ASCO_FUTURE_NOCORO_H

#include <mutex>
#include <condition_variable>
#include <functional>
#include <new>

#include <asco/utils/utils.h>

namespace asco {

class runtime;

template<typename T>
requires is_move_secure_v<T>
class __future_nocoro;

template<typename T>
requires is_move_secure_v<T>
using future_nocoro = std::shared_ptr<__future_nocoro<T>>;

template<typename T>
requires is_move_secure_v<T>
class __future_nocoro {
private:
    std::function<T()> exef;
    std::function<void()> workerf;
    T *val;

    std::mutex mutex;
    std::condition_variable cv;
    bool ok{false};

    bool awaited{false};
    bool moved{false};

public:
    __future_nocoro(std::function<T()> &f, std::shared_ptr<__future_nocoro<T>> pself)
        : exef(f), val((T *)(new T)), workerf([pself]() {
            *pself->val = std::move(pself->exef());
            pself->ok = true;
            pself->cv.notify_one();
        }) {}

    ~__future_nocoro() {
        if (!moved && val) {
            delete val;
            val = nullptr;
        }
    }

    static future_nocoro<T> construct(std::function<T()> f) {
        auto p = std::shared_ptr<__future_nocoro<T>>(
            (__future_nocoro<T> *)(new uint8_t[sizeof(__future_nocoro<T>)]),
            [](__future_nocoro<T> *p){
                delete[] (uint8_t *)p;
            });
        new (p.get()) __future_nocoro<T>(f, p);
        return p;
    }

    std::function<void()>& get_workerf() {
        return workerf;
    }

    T await() {
        if (awaited)
            throw std::runtime_error("[ASCO] future_nocoro::await() called twice");
        if (moved)
            throw std::runtime_error("[ASCO] future_nocoro::await() called after move");

        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [this] { return ok; });
        lk.unlock();
        awaited = true;
        return std::move(*val);
    }
};

struct future_void {
    future_void() = default;
};

};

#endif
