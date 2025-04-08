#ifndef ASCO_SYNC_AWAITER_H
#define ASCO_SYNC_AWAITER_H

#include <asco/utils/channel.h>

namespace asco {

struct sync_awaiter {
    asco_inner::shared_receiver<__u8> rx;

    __always_inline void await() {
        rx->recv();
    }
};

};

#endif
