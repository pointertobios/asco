// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <expected>
#include <memory>
#include <optional>
#include <tuple>

#include <asco/concurrency/ring_queue.h>
#include <asco/future.h>
#include <asco/panic.h>
#include <asco/sync/semaphore.h>
#include <asco/util/types.h>

namespace asco::sync {

namespace detail {

constexpr std::size_t channel_capacity = 1024;

template<typename T>
using q_sender = concurrency::ring_queue::sender<T, channel_capacity>;

template<typename T>
using q_receiver = concurrency::ring_queue::receiver<T, channel_capacity>;

struct channel_cntrl {
    std::atomic_bool closed{false};
    semaphore<channel_capacity> count_sem{0};
    semaphore<channel_capacity> backpress_sem{channel_capacity};
};

};  // namespace detail

template<util::types::move_secure T>
class receiver;

template<util::types::move_secure T>
class sender final {
    template<util::types::move_secure U>
    friend std::tuple<sender<U>, receiver<U>> channel();

public:
    sender() = default;

    sender(const sender &rhs)
            : m_sender{rhs.m_sender}
            , m_sem_cntrl{rhs.m_sem_cntrl} {}

    sender &operator=(const sender &rhs) {
        if (this != &rhs) {
            this->~receiver();
            new (this) sender{rhs};
        }
        return *this;
    }

    sender(sender &&rhs)
            : m_sender{std::move(rhs.m_sender)}
            , m_sem_cntrl{std::move(rhs.m_sem_cntrl)} {}

    sender &operator=(sender &&rhs) {
        if (this != &rhs) {
            this->~sender();
            new (this) sender{std::move(rhs)};
        }
        return *this;
    }

    future<std::expected<std::monostate, util::types::monostate_if_void<T>>>
    send(util::types::monostate_if_void<T> value)
        requires(!std::is_void_v<T>)
    {
        asco_assert_lint(m_sem_cntrl, "asco::sync::sender: 发送端没有绑定到队列");

        if (m_sem_cntrl->closed.load(std::memory_order::acquire)) {
            co_return std::unexpected{std::move(value)};
        }
        co_await m_sem_cntrl->backpress_sem.acquire();
        if (m_sem_cntrl->closed.load(std::memory_order::acquire)) {
            m_sem_cntrl->backpress_sem.release();
            co_return std::unexpected{std::move(value)};
        }
        m_sender.try_send(std::move(value));
        m_sem_cntrl->count_sem.release();
        co_return {};
    }

    future<bool> send()
        requires(std::is_void_v<T>)
    {
        asco_assert_lint(m_sem_cntrl, "asco::sync::sender: 发送端没有绑定到队列");

        if (m_sem_cntrl->closed.load(std::memory_order::acquire)) {
            co_return false;
        }
        co_await m_sem_cntrl->backpress_sem.acquire();
        if (m_sem_cntrl->closed.load(std::memory_order::acquire)) {
            m_sem_cntrl->backpress_sem.release();
            co_return false;
        }
        m_sender.try_send();
        m_sem_cntrl->count_sem.release();
        co_return true;
    }

    void stop() {
        asco_assert_lint(m_sem_cntrl, "asco::sync::sender: 发送端没有绑定到队列");

        m_sem_cntrl->closed.store(true, std::memory_order::release);
        m_sem_cntrl->count_sem.release();
        m_sem_cntrl->backpress_sem.release();
    }

private:
    sender(detail::q_sender<T> tx, std::shared_ptr<detail::channel_cntrl> s)
            : m_sender{tx}
            , m_sem_cntrl{s} {}

    detail::q_sender<T> m_sender{};
    std::shared_ptr<detail::channel_cntrl> m_sem_cntrl;
};

template<util::types::move_secure T>
class receiver final {
    template<util::types::move_secure U>
    friend std::tuple<sender<U>, receiver<U>> channel();

public:
    receiver() = default;

    receiver(const receiver &rhs)
            : m_receiver{rhs.m_receiver}
            , m_sem_cntrl{rhs.m_sem_cntrl} {}

    receiver &operator=(const receiver &rhs) {
        if (this != &rhs) {
            this->~receiver();
            new (this) receiver{rhs};
        }
        return *this;
    }

    receiver(receiver &&rhs)
            : m_receiver{std::move(rhs.m_receiver)}
            , m_sem_cntrl{std::move(rhs.m_sem_cntrl)} {}

    receiver &operator=(receiver &&rhs) {
        if (this != &rhs) {
            this->~receiver();
            new (this) receiver{std::move(rhs)};
        }
        return *this;
    }

    future<std::conditional_t<std::is_void_v<T>, bool, std::optional<T>>> recv() {
        asco_assert_lint(m_sem_cntrl, "asco::sync::receiver: 接收端没有绑定到队列");

        if (!m_sem_cntrl->count_sem.get_count() && m_sem_cntrl->closed.load(std::memory_order::acquire)) {
            co_return std::nullopt;
        }
        co_await m_sem_cntrl->count_sem.acquire();
        if (!m_sem_cntrl->count_sem.get_count() && m_sem_cntrl->closed.load(std::memory_order::acquire)) {
            co_return std::nullopt;
        }
        auto res = m_receiver.try_recv();
        m_sem_cntrl->backpress_sem.release();
        co_return res;
    }

    void stop() {
        asco_assert_lint(m_sem_cntrl, "asco::sync::receiver: 接收端没有绑定到队列");

        m_sem_cntrl->closed.store(true, std::memory_order::release);
        m_sem_cntrl->count_sem.release();
        m_sem_cntrl->backpress_sem.release();
    }

private:
    receiver(detail::q_receiver<T> rx, std::shared_ptr<detail::channel_cntrl> s)
            : m_receiver{rx}
            , m_sem_cntrl{s} {}

    detail::q_receiver<T> m_receiver{};
    std::shared_ptr<detail::channel_cntrl> m_sem_cntrl;
};

template<util::types::move_secure T>
std::tuple<sender<T>, receiver<T>> channel() {
    auto cntrl = std::make_shared<detail::channel_cntrl>();
    auto [tx, rx] = concurrency::ring_queue::create<T, detail::channel_capacity>();
    return {sender<T>{tx, cntrl}, receiver<T>{rx, cntrl}};
}

};  // namespace asco::sync
