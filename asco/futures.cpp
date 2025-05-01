// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/futures.h>

#include <cstring>

#include <asco/utils/pubusing.h>

namespace asco::futures::inner {

size_t clone(std::coroutine_handle<> h) {
    // Clone coroutine state structure
    size_t *src = reinterpret_cast<size_t *>(h.address()) - 1;
    size_t n = *src;
    size_t *dst = reinterpret_cast<size_t *>(::operator new(n + sizeof(size_t)));
    std::memcpy(dst, src, n + sizeof(size_t));
    auto coh = std::coroutine_handle<>::from_address(dst + 1);

    // Spawn coroutine and clone coroutine local frame
    size_t src_id = RT::get_runtime()->task_id_from_corohandle(h);
    auto src_worker = RT::__worker::get_worker_from_task_id(src_id);
    auto src_task = src_worker->sc.get_task(src_id);
    auto src_frame = src_task->coro_local_frame;
    size_t dst_id;
    if (src_task->is_blocking)
        dst_id = RT::get_runtime()->spawn_blocking(coh, src_frame->prev, src_id);
    else
        dst_id = RT::get_runtime()->spawn(coh, src_frame->prev, src_id);

    auto dst_worker = RT::__worker::get_worker_from_task_id(dst_id);
    auto dst_frame = dst_worker->sc.get_task(dst_id)->coro_local_frame;
    dst_frame->vars = src_frame->vars;

    auto sem = src_worker->sc.clone_sync_awaiter(src_id);
    if (sem)
        dst_worker->sc.emplace_sync_awaiter(dst_id, sem);

    return dst_id;
}

};  // namespace asco::futures::inner
