#include <asco/core/daemon.h>

#include <cstring>
#ifdef __linux__
#    include <pthread.h>
#    include <signal.h>
#endif

namespace asco::core {

daemon::daemon(const char *name, int sig)
        : name(name)
        , awake_sig(sig) {}

daemon::~daemon() {
    running.store(false, morder::seq_cst);
    awake();
}

void daemon::awake() {
    spin_wait_init();
#ifdef __linux__
    ::pthread_kill(ptid, awake_sig);
#elifdef _WIN32
#    error "Windows timer not implemented"
#endif
}

void daemon::spin_wait_init() { while (!init_waiter.load(morder::seq_cst)); }

void daemon::start() {
    thread = std::jthread([this]() {
#ifdef __linux__
        ptid = ::pthread_self();
        ::pthread_setname_np(ptid, name);

        pid = ::gettid();

        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = [](int) {};
        ::sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        ::sigaction(awake_sig, &sa, nullptr);
#elifdef _WIN32
#    error "Windows timer not implemented"
#endif
        init_waiter.store(true, morder::seq_cst);

        while (running.load(morder::seq_cst)) run();
    });
}

};  // namespace asco::core
