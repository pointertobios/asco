#ifndef ASCO_SCHED_H
#define ASCO_SCHED_H

#include <chrono>
#include <concepts>
#include <coroutine>
#include <optional>

namespace asco::sched {

struct task {
    using task_id = size_t;

    task_id id;
    std::coroutine_handle<> handle;
};

template<typename T>
concept is_scheduler = requires(T t) {
    { t.push_task(task{}) } -> std::same_as<void>;
    { t.sched() } -> std::same_as<std::optional<std::coroutine_handle<>>>;
};

// I call it std_scheduler because it uses STL.
class std_scheduler {
public:
    std_scheduler();
    void push_task(task t);
    std::optional<std::coroutine_handle<>> sched();
};

};

#endif
