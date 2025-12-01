// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <optional>
#include <tuple>

#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco::queue {

enum class push_fail {
    full,   // Queue is full now.
    closed  // This queue is closed.
};

enum class pop_fail {
    non_object,  // No object available now.
    closed       // This queue is closed.
};

// Concept for the sender of a MPMC queue.
// Never stop automatically.
template<typename S, typename T>
concept sender = requires(S s) {
    { S::max_volume } -> std::same_as<const types::size_t &>;
    { s.push(std::declval<types::passing<T>>()) } -> std::same_as<std::optional<std::tuple<T, push_fail>>>;
    { s.stop() } -> std::same_as<void>;
    { s.is_stopped() } -> std::same_as<bool>;
} && concepts::move_secure<S> && concepts::move_secure<T>;

// Concept for the receiver of a MPMC queue.
// Never stop automatically.
template<typename R, typename T>
concept receiver = requires(R r) {
    { R::max_volume } -> std::same_as<const types::size_t &>;
    { r.pop() } -> std::same_as<std::expected<T, pop_fail>>;
    { r.stop() } -> std::same_as<void>;
    { r.is_stopped() } -> std::same_as<bool>;
} && concepts::move_secure<R> && concepts::move_secure<T>;

template<typename C, typename T>
concept creator = requires(const C c) {
    typename C::Sender;
    typename C::Receiver;
    { C::max_volume } -> std::same_as<const types::size_t &>;
    { c.operator()() } -> std::same_as<std::tuple<typename C::Sender, typename C::Receiver>>;
};

};  // namespace asco::queue

namespace asco {

using queue::pop_fail;
using queue::push_fail;

};  // namespace asco
