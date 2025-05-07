// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SELECT_H
#define ASCO_SELECT_H 1

#include <coroutine>
#include <functional>
#include <semaphore>

#include <asco/core/taskgroup.h>
#include <asco/future.h>
#include <asco/futures.h>
#include <asco/utils/pubusing.h>

namespace asco {

template<size_t N>
    requires(N > 1)
class select {
public:
    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        auto &rt = RT::get_runtime();
        auto currid = rt.task_id_from_corohandle(handle);
        rt.join_task_to_group(currid, currid, true);

        if (futures::inner::group_local_exists<__consteval_str_hash("__asco_select_sem__")>())
            del_glocal("__asco_select_sem__");
        std::binary_semaphore decl_glocal(__asco_select_sem__, new std::binary_semaphore{1});

        size_t h[N];
        for (size_t i{1}; i < N; i++) {
            n = i;
            h[i] = futures::inner::clone(handle);
        }
        n = 0;

        for (size_t i{1}; i < N; i++) {
            rt.awake(h[i]);
        }

        return false;
    }

    size_t await_resume() { return n; }

private:
    size_t n{0};
};

};  // namespace asco

#endif
