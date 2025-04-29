// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SELECT_H
#define ASCO_SELECT_H 1

#include <barrier>
#include <coroutine>

#include <asco/core/taskgroup.h>
#include <asco/future.h>
#include <asco/futures.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/pubusing.h>

namespace asco {

template<size_t N>
class select {
public:
    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        auto currid = RT::get_runtime()->task_id_from_corohandle(handle);
        auto runtime = RT::get_runtime();
        runtime->join_task_to_group(currid, currid);

        if (futures::inner::group_local_exists<__consteval_str_hash("__asco_select_barrier__")>())
            del_glocal("__asco_select_barrier__");
        std::barrier<std::function<void()>> decl_glocal(
            __asco_select_barrier__, new std::barrier<std::function<void()>>(N, [] {}));

        if (futures::inner::group_local_exists<__consteval_str_hash("__asco_select_sem__")>())
            del_glocal("__asco_select_sem__");

        binary_semaphore decl_glocal(__asco_select_sem__, new binary_semaphore{1});
        for (size_t i{1}; i < N; i++) {
            n = i;
            auto h = futures::inner::clone(handle);
        }
        n = 0;
        return false;
    }

    size_t await_resume() {
        std::barrier<std::function<void()>> group_local(__asco_select_barrier__);
        __asco_select_barrier__.arrive_and_wait();
        return n;
    }

private:
    size_t n{0};
};

};  // namespace asco

#endif
