#include <asco/sched.h>

#include <iostream>

namespace asco::sched {

    std_scheduler::std_scheduler() {}

    void std_scheduler::push_task(task t) {
        tasks.push_back(task_control{t, std_scheduler::task_control::__control_state::running});
    }

    std::optional<task> std_scheduler::sched() {
        if (tasks.empty()) {
            return std::nullopt;
        }
        using std_scheduler::task_control::__control_state::running;
        using std_scheduler::task_control::__control_state::suspending;
        std::erase_if(tasks, [](auto &t) {
            bool b = t.t.done();
            if (b)
                t.t.handle.destroy();
            return b;
        });
        for (auto it = tasks.begin(); it != tasks.end(); it++) {
            if (it->state == running) {
                auto task = std::move(*it);
                tasks.erase(it);
                tasks.push_back(task);
                return task.t;
            }
        }
        return std::nullopt;
    }

    bool std_scheduler::currently_finished_all() {
        return tasks.empty();
    }

    void std_scheduler::awake(task::task_id id) {
        for (auto &t : tasks) {
            if (t.t.id == id) {
                std::cout << std::format("std_scheduler::awake({}) awaked\n", id);
                t.state = std_scheduler::task_control::__control_state::running;
                break;
            }
        }
    }

    void std_scheduler::suspend(task::task_id id) {
        for (auto &t : tasks) {
            if (t.t.id == id) {
                t.state = std_scheduler::task_control::__control_state::suspending;
                break;
            }
        }
    }

    bool std_scheduler::task_exists(task::task_id id) {
        for (auto &t: tasks) {
            if (t.t.id == id)
                return true;
        }
        return false;
    }

};
