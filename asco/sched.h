#ifndef ASCO_SCHED_H
#define ASCO_SCHED_H

#include <chrono>
#include <concepts>
#include <coroutine>
#include <optional>
#include <vector>

namespace asco::sched {

struct task {
    using task_id = size_t;

    task_id id;
    std::coroutine_handle<> handle;

    __always_inline bool operator==(task &rhs) const {
        return id == rhs.id;
    }

    __always_inline void resume() const {
        handle.resume();
    }

    __always_inline bool done() const {
        return handle.done();
    }
};

template<typename T>
concept is_scheduler = requires(T t) {
    { t.push_task(task{}) } -> std::same_as<void>;
    { t.sched() } -> std::same_as<std::optional<task>>;
    { t.exit(std::declval<task &>()) } -> std::same_as<void>;
    { t.currently_finished_all() } -> std::same_as<bool>;
};

// I call it std_scheduler because it uses STL.
class std_scheduler {
public:
    std_scheduler();
    void push_task(task t);
    std::optional<task> sched();
    void exit(task &t);
    bool currently_finished_all();

private:
    std::vector<task> tasks;
};

};

#endif
