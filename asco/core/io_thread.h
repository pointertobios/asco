#ifndef ASCO_CORE_IO_THREAD_H
#define ASCO_CORE_IO_THREAD_H 1

#include <thread>

#include <asco/core/daemon.h>
#include <asco/utils/channel.h>
#include <asco/utils/pubusing.h>

namespace asco::core::io {

using namespace types;

class request {};

class io_thread : public daemon {
public:
    explicit io_thread();

    void submit_request(request req);

private:
    void run() override;
};

};  // namespace asco::core::io

#endif
