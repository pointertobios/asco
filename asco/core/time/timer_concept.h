// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <variant>

#include <asco/core/time/high_resolution_timer.h>
#include <asco/core/time/timer.h>

namespace asco::core::time {

class timer_concept {
public:
    template<typename T>
    timer_concept(std::unique_ptr<T> t)
        requires timer<T>
            : timer{std::move(t)} {}

    timer_id register_timer(
        const std::chrono::high_resolution_clock::time_point &expire_time, worker &worker_ptr,
        task_id task_id) {
        return std::visit(
            [&expire_time, &worker_ptr, task_id](auto &t) {
                return t->register_timer(expire_time, worker_ptr, task_id);
            },
            timer);
    }

    void unregister_timer(timer_id id) {
        std::visit([id](auto &t) { t->unregister_timer(id); }, timer);
    }

private:
    template<typename T>
    using up = std::unique_ptr<T>;

    std::variant<up<high_resolution_timer>> timer;
};

};  // namespace asco::core::time
