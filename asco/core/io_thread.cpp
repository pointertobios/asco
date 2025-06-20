#include <asco/core/io_thread.h>

#ifdef __linux__
#    include <pthread.h>
#    include <signal.h>
#endif

namespace asco::core::io {

using namespace std::chrono_literals;

io_thread::io_thread()
        : daemon("asco::io_thread", SIGALRM) {
    daemon::start();
}

void io_thread::submit_request(request req) {}

void io_thread::run() { std::this_thread::interruptable_sleep_for(5s); }

};  // namespace asco::core::io
