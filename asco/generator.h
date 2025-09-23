// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_GENERATOR_H
#define ASCO_GENERATOR_H 1

#include <asco/future.h>
#include <asco/nolock/continuous_queue.h>
#include <asco/rterror.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/concepts.h>

namespace asco::base {

using namespace concepts;

namespace cq = continuous_queue;

struct generator_extra {
    unlimited_semaphore yield_sem{0};
    atomic_size_t yield_count{0};
    atomic_bool throwed{false};
};

template<move_secure T, bool Blocking>
struct generator_base : public future_base<T, false, Blocking, sizeof(generator_extra)> {
    using base_future = future_base<T, false, Blocking, sizeof(generator_extra)>;

    struct promise_type;
    using corohandle = base_future::corohandle;
    using return_type = typename base_future::return_type;

    struct promise_type : base_future::promise_base {
        using promise_base = typename base_future::promise_base;

        cq::sender<return_type> yield_tx;

        generator_extra *gextra;

        corohandle corohandle_from_promise() override {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }

        generator_base get_return_object() {
            gextra = new (promise_base::state->extra_space) generator_extra;
            promise_base::state->extra_space_dtor =
                +[](void *p) { reinterpret_cast<generator_extra *>(p)->~generator_extra(); };

            promise_base::state->has_return_value = false;

            auto [tx, rx] = cq::create<return_type>();
            yield_tx = std::move(tx);

            generator_base res;
            static_cast<base_future &>(res) =
                promise_base::get_return_object();  // `future_base` and `generator_base` has no virtual table
                                                    // and dependent members, directly assign is ok.
            res.gextra = gextra;
            res.yield_rx = std::move(rx);
            return std::move(res);
        }

        auto yield_value(return_type &&value) {
            if (yield_tx.push(std::move(value))) [[unlikely]] {
                throw runtime_error("[ASCO] generator yield failed: receiver unexpectedly closed");
            }
            gextra->yield_sem.release();
            gextra->yield_count.fetch_add(1, morder::release);

            return std::suspend_never{};
        }

        void return_void() {
            yield_tx.stop();
            gextra->yield_sem.release();

            if (promise_base::handle_select_winner())
                return;

            promise_base::state->returned.store(true);
        }

        void unhandled_exception() {
            promise_base::unhandled_exception();

            yield_tx.stop();
            gextra->yield_sem.release();
            gextra->throwed.store(true, morder::release);
        }
    };

    operator bool() const { return !yield_rx.is_stopped(); }

    auto operator()() -> future_inline<return_type> {
        co_await gextra->yield_sem.acquire();
        if (gextra->throwed.load(morder::acquire) && gextra->yield_count.load(morder::acquire) == 0) {
            if (auto &e = base_future::state->e) {
                auto tmp = e;
                e = nullptr;
                std::rethrow_exception(tmp);
            }
        } else {
            gextra->yield_count.fetch_sub(1, morder::release);
        }
        if (auto res = yield_rx.pop())
            co_return std::move(res.value());
        else
            throw runtime_error("[ASCO] generator fetch failed: generator ends");
    }

    bool await_ready() = delete;

    auto await_suspend(std::coroutine_handle<>) = delete;

    return_type await_resume() = delete;

    return_type await() = delete;

    generator_base() = default;

    generator_base(const generator_base &) = delete;

    generator_base(generator_base &&rhs)
            : base_future(std::move(rhs))
            , yield_rx(std::move(rhs.yield_rx))
            , gextra(rhs.gextra) {
        rhs.gextra = nullptr;
    }

    generator_base &operator=(generator_base &&rhs) {
        if (this == &rhs)
            return *this;

        this->~generator_base();
        new (this) generator_base(std::move(rhs));
        return *this;
    }

private:
    cq::receiver<return_type> yield_rx;
    generator_extra *gextra{nullptr};
};

};  // namespace asco::base

namespace asco {

template<concepts::move_secure T>
using generator = base::generator_base<T, false>;
template<concepts::move_secure T>
using generator_core = base::generator_base<T, true>;

};  // namespace asco

#endif
