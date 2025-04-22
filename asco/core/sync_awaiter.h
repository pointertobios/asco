// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_AWAITER_H
#define ASCO_SYNC_AWAITER_H

#include <asco/utils/channel.h>

namespace asco {

struct sync_awaiter {
    inner::shared_receiver<__u8> rx;

    __always_inline void await() {
        rx->recv();
    }
};

};

#endif
