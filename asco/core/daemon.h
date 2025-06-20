#ifndef ASCO_CORE_DAEMON_H
#define ASCO_CORE_DAEMON_H 1

#include <thread>

#include <asco/utils/pubusing.h>

namespace asco::core {

using namespace types;

class daemon {
public:
    explicit daemon(const char *name, int sig);
    ~daemon();

    void awake();
    void spin_wait_init();

    int native_thread_id() const { return pid; };

protected:
    void start();

private:
    virtual void run() = 0;

    const char *name;
    int awake_sig;

    atomic_bool running{true};
    atomic_bool init_waiter{false};
    std::jthread thread;

    ::pthread_t ptid;
    int pid;  // Actually tid on linux and handle on windows, usually used to set cpu affinity.
};

};  // namespace asco::core

#endif
