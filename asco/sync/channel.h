// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <tuple>

#include <asco/concurrency/continuous_queue.h>
#include <asco/concurrency/queue.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace concepts;
using namespace types;

namespace cq = continuous_queue;

template<move_secure T, queue::sender<T> QSender = cq::sender<T>>
class sender {
public:
    sender() = default;

    sender(QSender &&qsender, std::shared_ptr<unlimited_semaphore> sem)
            : queue_sender{std::move(qsender)}
            , sem{std::move(sem)} {}

    sender(const sender &rhs) = default;
    sender &operator=(const sender &rhs) = default;

    sender(sender &&) = default;
    sender &operator=(sender &&) = default;

    [[nodiscard("[ASCO] sender::send() maybe returns a T value")]] std::optional<T> send(passing<T> val) {
        auto res = queue_sender.push(std::move(val));
        if (!res.has_value()) {
            sem->release();
        }
        return res;
    }

    void stop() noexcept { queue_sender.stop(); }

    bool is_stopped() const noexcept { return queue_sender.is_stopped(); }

private:
    QSender queue_sender;
    std::shared_ptr<unlimited_semaphore> sem;
};

template<move_secure T, queue::receiver<T> QReceiver = cq::receiver<T>>
class receiver {
public:
    receiver() = default;

    receiver(QReceiver &&qreceiver, std::shared_ptr<unlimited_semaphore> sem)
            : queue_receiver{std::move(qreceiver)}
            , sem{std::move(sem)} {}

    receiver(const receiver &rhs) = default;
    receiver &operator=(const receiver &rhs) = default;

    receiver(receiver &&) = default;
    receiver &operator=(receiver &&) = default;

    std::expected<T, queue::pop_fail> try_recv() {
        if (!sem->try_acquire()) {
            return std::unexpected(queue::pop_fail::non_object);
        } else {
            return queue_receiver.pop();
        }
    }

    [[nodiscard("[ASCO] receiver::receive() maybe fails")]] future<std::optional<T>> recv() {
        co_await sem->acquire();
        if (auto res = queue_receiver.pop(); res.has_value()) {
            co_return *res;
        } else {
            co_return std::nullopt;
        }
    }

    bool is_stopped() const noexcept { return queue_receiver.is_stopped(); }

private:
    QReceiver queue_receiver;
    std::shared_ptr<unlimited_semaphore> sem;
};

template<move_secure T, queue::creator<T> Creator>
auto channel(Creator ctor) {
    auto [tx, rx] = ctor();
    auto sem = std::make_shared<unlimited_semaphore>(0);
    return std::tuple{
        sender<T, typename Creator::Sender>{std::move(tx), sem},
        receiver<T, typename Creator::Receiver>{std::move(rx), sem}};
}

template<move_secure T>
auto channel() {
    return channel<T>(cq::create<T>);
}

};  // namespace asco::sync

namespace asco {

using sync::channel;
using sync::receiver;
using sync::sender;

};  // namespace asco
