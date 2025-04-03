#ifndef ASCO_SCHED_H
#define ASCO_SCHED_H

#include <atomic>
#include <chrono>
#include <concepts>
#include <mutex>
#include <coroutine>
#include <optional>
#include <set>
#include <vector>

namespace asco::sched {

struct task {
    using task_id = size_t;

    task_id id;
    std::coroutine_handle<> handle;

    bool is_blocking;

    __always_inline bool operator==(task &rhs) const {
        return id == rhs.id;
    }

    __always_inline void resume() const {
        if (handle.done())
            throw std::runtime_error("[ASCO] Inner error: task is done but not destroyed.");
        handle.resume();
    }

    __always_inline bool done() const {
        bool b = handle.done();
        if (b)
            handle.destroy();
        return b;
    }
};

template<typename T>
concept is_scheduler = requires(T t) {
    { t.push_task(task{}) } -> std::same_as<void>;
    { t.sched() } -> std::same_as<std::optional<task>>;
    { t.currently_finished_all() } -> std::same_as<bool>;
    { t.awake(task::task_id{}) } -> std::same_as<void>;
    { t.suspend(task::task_id{}) } -> std::same_as<void>;
    { t.destroy(task::task_id{}) } -> std::same_as<void>;
    { t.task_exists(task::task_id{}) } -> std::same_as<bool>;
};

// I call it std_scheduler because it uses STL.
class std_scheduler {
public:
    struct task_control {
        task t;
        enum class __control_state {
            running,
            suspending,
        } state;
    };

    std_scheduler();
    void push_task(task t);
    std::optional<task> sched();
    bool currently_finished_all();

    void awake(task::task_id id);
    void suspend(task::task_id id);
    void destroy(task::task_id id);

    bool task_exists(task::task_id id);

private:
    std::vector<task_control *> tasks;
    // During `sched()` `awake()` and `suspend()` calling,
    // `tasks` may be modified, the iterator will be invalidated during the iteration.
    // Lock it to prevent this.
    std::mutex tasks_mutex;
};

};

#endif
