#include <asco/sched.h>

#include <iostream>

namespace asco::sched {

    std_scheduler::std_scheduler() {}

    void std_scheduler::push_task(task t) {
        tasks.push_back(new task_control{t, std_scheduler::task_control::__control_state::suspending});
    }

    std::optional<task> std_scheduler::sched() {
        if (tasks.empty()) {
            return std::nullopt;
        }
        using std_scheduler::task_control::__control_state::running;
        using std_scheduler::task_control::__control_state::suspending;
        std::lock_guard lk{tasks_mutex};
        for (auto it = tasks.begin(); it != tasks.end(); it++) {
            if ((*it)->state == running) {
                auto task = *it;
                tasks.erase(it);
                tasks.push_back(task);
                return task->t;
            }
        }
        return std::nullopt;
    }

    bool std_scheduler::currently_finished_all() {
        return tasks.empty();
    }

    void std_scheduler::awake(task::task_id id) {
        std::lock_guard lk{tasks_mutex};
        for (auto &t : tasks) {
            if (t->t.id == id) {
                t->state = std_scheduler::task_control::__control_state::running;
                return;
            }
        }
        throw std::runtime_error("[ASCO] Inner error: task not found in scheduler.");
    }

    void std_scheduler::suspend(task::task_id id) {
        std::lock_guard lk{tasks_mutex};
        for (auto &t : tasks) {
            if (t->t.id == id) {
                t->state = std_scheduler::task_control::__control_state::suspending;
                break;
            }
        }
    }

    void std_scheduler::destroy(task::task_id id) {
        std::lock_guard lk{tasks_mutex};
        for (auto it = tasks.begin(); it != tasks.end(); ++it) {
            if ((*it)->t.id == id) {
                tasks.erase(it);
                break;
            }
        }
    }

    bool std_scheduler::task_exists(task::task_id id) {
        std::lock_guard lk{tasks_mutex};
        for (auto &t: tasks) {
            if (t->t.id == id)
                return true;
        }
        return false;
    }

};
