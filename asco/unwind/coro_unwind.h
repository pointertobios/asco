// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UNWIND_CORO_UNWIND_H
#define ASCO_UNWIND_CORO_UNWIND_H 1

#include <asco/future.h>
#include <asco/unwind/unwind.h>
#include <execinfo.h>

namespace asco::unwind {

class coro_unwinder : public function_unwinder {
public:
    template<typename R = RT>
        requires core::is_runtime<R>
    static stacktrace unwind() {
        coro_unwinder unwinder;

        void *stackrets[130];
        int len = ::backtrace(stackrets, 130);
        for (int i = 0; i < len - 2; i++) {
            auto s = unwinder.resolve_address((size_t)stackrets[i]);
            if (s.name.ends_with("[clone .resume]") || s.name.ends_with("[clone .actor]"))
                break;
            unwinder.stacks.push_back(s);
        }

        if (unwinder.stacks.size() < 128) {
            auto &task = RT::__worker::get_worker().current_task();
            for (coro_trace *trace = &task.coro_local_frame->tracing_stack; trace; trace = trace->prev)
                unwinder.stacks.push_back(unwinder.resolve_address((size_t)trace->current));
        }

        return {std::move(unwinder.stacks)};
    }
};

};  // namespace asco::unwind

#endif
