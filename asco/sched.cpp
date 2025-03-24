#include <asco/sched.h>

namespace asco::sched {

    std_scheduler::std_scheduler() {}

    void std_scheduler::push_task(task t) {
        tasks.push_back(t);
    }

    std::optional<task> std_scheduler::sched() {
        if (tasks.empty()) {
            return std::nullopt;
        }
        auto task = std::move(tasks.front());
        tasks.erase(tasks.begin());
        tasks.push_back(std::move(task));
        return tasks.back();
    }

    void std_scheduler::exit(task &t) {
        t.handle.destroy();
        if (auto it = std::find(tasks.begin(), tasks.end(), t); it != tasks.end())
            tasks.erase(it);
    }

    bool std_scheduler::currently_finished_all() {
        return tasks.empty();
    }

};
