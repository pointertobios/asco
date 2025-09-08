// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/futures.h>

#include <cstring>

#include <asco/coroutine_allocator.h>
#include <asco/utils/pubusing.h>

namespace asco::base::this_coro::inner {

size_t clone(std::coroutine_handle<> h) {
    auto &rt = RT::get_runtime();

    // Clone coroutine state structure
    size_t *src =
        reinterpret_cast<size_t *>(h.address()) - (coroutine_allocator::header_size / sizeof(size_t));
    size_t n = *src;
    size_t *dst = reinterpret_cast<size_t *>(coroutine_allocator::allocate(n))
                  - (coroutine_allocator::header_size / sizeof(size_t));
    std::memcpy(dst, src, n + coroutine_allocator::header_size);
    auto coh =
        std::coroutine_handle<>::from_address(dst + (coroutine_allocator::header_size / sizeof(size_t)));

    // Spawn coroutine and clone coroutine local frame
    size_t src_id = rt.task_id_from_corohandle(h);
    auto &src_worker = RT::__worker::get_worker_from_task_id(src_id);
    auto src_task = src_worker.sc.get_task(src_id);
    auto src_frame = src_task->coro_local_frame;
    size_t dst_id;
    if (src_task->is_blocking)
        dst_id = rt.spawn_blocking(coh, src_frame->prev, src_task->coro_local_frame->tracing_stack, src_id);
    else
        dst_id = rt.spawn(coh, src_frame->prev, src_task->coro_local_frame->tracing_stack, src_id);

    auto &dst_worker = RT::__worker::get_worker_from_task_id(dst_id);
    auto dst_frame = dst_worker.sc.get_task(dst_id)->coro_local_frame;
    dst_frame->vars = src_frame->vars;

    auto sem = src_worker.sc.clone_sync_awaiter(src_id);
    if (sem)
        dst_worker.sc.emplace_sync_awaiter(dst_id, sem);

    return dst_id;
}

};  // namespace asco::base::this_coro::inner
