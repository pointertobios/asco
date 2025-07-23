// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_THREAD_H
#define ASCO_IO_THREAD_H 1

#include <asco/core/daemon.h>
#include <asco/utils/channel.h>
#include <asco/utils/pubusing.h>

namespace asco::core {

using namespace types;

class io_thread : public daemon {
public:
    explicit io_thread();

private:
    void run() override;
};

};  // namespace asco::core

#endif
