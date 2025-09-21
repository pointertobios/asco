// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_CORO_LOCAL_H
#define ASCO_CORO_LOCAL_H 1

#include <unordered_map>

#include <asco/compile_time/string.h>
#include <asco/core/slub.h>
#include <asco/perf.h>
#include <asco/rterror.h>
#include <asco/unwind/unwind.h>
#include <asco/utils/dynvar.h>
#include <asco/utils/pubusing.h>
#include <asco/utils/type_hash.h>

namespace asco::base {

using namespace types;

struct __coro_local_frame {
    __coro_local_frame *prev{nullptr};
    std::unordered_map<size_t, inner::dynvar> vars;

    // Just like other corolocal variables, tracing stack must be destroyed after all sub coroutine exited.
    unwind::coro_trace tracing_stack;

    atomic_size_t ref_count{1};

    __asco_always_inline __coro_local_frame(__coro_local_frame *prev, unwind::coro_trace trace)
            : prev(prev)
            , tracing_stack(trace) {
        if (prev)
            prev->subframe_enter();
    }

    __asco_always_inline ~__coro_local_frame() {
        if (prev && !prev->get_ref_count()) {
            delete prev;
        }

        for (auto &[_, v] : vars) { v.deconstruct(v.p); }
    }

    __asco_always_inline size_t get_ref_count() { return ref_count.load(morder::acquire); }

    __asco_always_inline void subframe_exit() {
        ref_count.fetch_sub(1, morder::seq_cst);
        if (prev)
            prev->subframe_exit();
    }

    __asco_always_inline void subframe_enter() {
        ref_count.fetch_add(1, morder::seq_cst);
        if (prev)
            prev->subframe_enter();
    }

    template<size_t Hash>
    bool var_exists() {
        if (vars.contains(Hash)) {
            return true;
        } else if (prev) {
            return prev->var_exists<Hash>();
        } else {
            return false;
        }
    }

    template<typename T, compile_time::string Name>
    T &get_var() {
        constexpr auto hash = inner::__consteval_str_hash(Name);

        if (auto it = vars.find(hash); it != vars.end()) {
            if (it->second.type != inner::type_hash<T>())
                throw asco::runtime_error(
                    "[ASCO] __coro_local_frame::get_var(): Coroutine local variable type mismatch");
            return *reinterpret_cast<T *>(it->second.p);
        } else if (prev) {
            return prev->get_var<T, Name>();
        } else {
            throw asco::runtime_error(
                std::format(
                    "[ASCO] __coro_local_frame::get_var(): Coroutine local variable \'{}\' not found",
                    static_cast<const char *>(Name)));
        }
    }

    template<typename T, compile_time::string Name>
    T &decl_var(T *pt, inner::dynvar::destructor destructor) {
        constexpr auto hash = inner::__consteval_str_hash(Name);

        if (auto it = vars.find(hash); it != vars.end())
            throw asco::runtime_error(
                std::format(
                    "[ASCO] Coroutine local variable \'{}\' already declared",
                    static_cast<const char *>(Name)));

        vars[hash] = inner::dynvar{inner::type_hash<T>(), pt, destructor};
        return *pt;
    }

    template<typename T, compile_time::string Name>
    T &decl_var(T *pt) {
        return decl_var<T, Name>(pt, [](void *p) { delete reinterpret_cast<T *>(p); });
    }

    template<typename T, compile_time::string Name>
    T &decl_var() {
        return decl_var<T, Name>(new T);
    }

    void *operator new(std::size_t) noexcept { return slub_cache.allocate(); }
    void operator delete(void *p) noexcept { slub_cache.deallocate(static_cast<__coro_local_frame *>(p)); }

private:
    static core::slub::cache<__coro_local_frame> slub_cache;
};

inline core::slub::cache<__coro_local_frame> __coro_local_frame::slub_cache{};

};  // namespace asco::base

#define coro_local(name)               \
    &name = RT::__worker::get_worker() \
                .current_task()        \
                .coro_local_frame->get_var<std::remove_reference_t<decltype(name)>, #name>()

#define decl_local_1arg(name)          \
    &name = RT::__worker::get_worker() \
                .current_task()        \
                .coro_local_frame->decl_var<std::remove_reference_t<decltype(name)>, #name>()

#define decl_local_2arg(name, ptr)     \
    &name = RT::__worker::get_worker() \
                .current_task()        \
                .coro_local_frame->decl_var<std::remove_reference_t<decltype(name)>, #name>(ptr)

#define decl_local_3arg(name, ptr, destructor) \
    &name = RT::__worker::get_worker()         \
                .current_task()                \
                .coro_local_frame->decl_var<std::remove_reference_t<decltype(name)>, #name>(ptr, destructor)

#define decl_local(...) \
    __dispatch(__VA_ARGS__, decl_local_3arg, decl_local_2arg, decl_local_1arg)(__VA_ARGS__)

#define decl_local_array(name, ptr)                                                   \
    decl_local(                                                                       \
        name, ({                                                                      \
            auto *__ptr = new std::remove_reference_t<decltype(name)>{};              \
            *__ptr = ptr;                                                             \
            __ptr;                                                                    \
        }),                                                                           \
        [](void *p_) {                                                                \
            auto p = reinterpret_cast<std::remove_reference_t<decltype(name)> *>(p_); \
            delete[] *p;                                                              \
            delete p;                                                                 \
        })

#endif
