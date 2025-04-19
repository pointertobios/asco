// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_CORO_LOCAL_H
#define ASCO_CORO_LOCAL_H 1

#include <functional>
#include <unordered_map>

#include <asco/utils/pubusing.h>
#include <asco/utils/type_hash.h>

namespace asco {

struct __coro_local_var {
    size_t type;
    void *p;
    std::function<void (void *)> deconstruct;
};

struct __coro_local_frame {
    __coro_local_frame *prev{nullptr};
    std::unordered_map<size_t, __coro_local_var> vars;
    atomic_size_t ref_count{1};

    inline __coro_local_frame(__coro_local_frame *prev)
        : prev(prev) {
        if (prev)
            prev->subframe_enter();
    }

    inline ~__coro_local_frame() {
        if (prev && !prev->get_ref_count()) {
            delete prev;
        }

        for (auto &[_, v] : vars) {
            v.deconstruct(v.p);
        }
    }

    inline size_t get_ref_count() {
        return ref_count.load(morder::acquire);
    }

    inline void subframe_exit() {
        ref_count.fetch_sub(1, morder::seq_cst);
        if (prev)
            prev->subframe_exit();
    }

    inline void subframe_enter() {
        ref_count.fetch_add(1, morder::seq_cst);
        if (prev)
            prev->subframe_enter();
    }

    template<typename T, size_t hash>
    T &get_var(const char *name) {
        if (auto it = vars.find(hash); it != vars.end()) {
            if (it->second.type != type_hash<T>())
                throw std::runtime_error("[ASCO] Coroutine local variable type mismatch");
            return *reinterpret_cast<T *>(it->second.p);
        } else if (prev) {
            return prev->get_var<T, hash>(name);
        } else {
            throw std::runtime_error(std::format("[ASCO] Coroutine local variable \'{}\' not found", name));
        }
    }

    template<typename T, size_t hash>
    T &decl_var(const char *name, T *pt, std::function<void (void *)> destructor) {
        if (auto it = vars.find(hash); it != vars.end()) {
            throw std::runtime_error(std::format("[ASCO] Coroutine local variable \'{}\' already declared", name));
        } else {
            vars[hash] = __coro_local_var{type_hash<T>(), pt, destructor};
            return *pt;
        }
    }

    template<typename T, size_t hash>
    T &decl_var(const char *name, T *pt) {
        return decl_var<T, hash>(name, pt, [](void *p) { delete reinterpret_cast<T *>(p); });
    }

    template<typename T, size_t hash>
    T &decl_var(const char *name) {
        return decl_var<T, hash>(name, new T, [] (void *p) { delete reinterpret_cast<T *>(p); });
    }
};

};

#define coro_local(name)                                                                        \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                         \
        ->get_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name);

#define decl_local_1arg(name)                                                                           \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                                 \
        ->decl_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name)
#define decl_local_2arg(name, ptr)                                                                      \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                                 \
        ->decl_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name, ptr)
#define decl_local_3arg(name, ptr, destructor)                                                                  \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                                         \
        ->decl_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name, ptr, destructor)

#define dispatch(_1, _2, _3, NAME, ...) NAME
#define decl_local(...) \
    dispatch(__VA_ARGS__, decl_local_3arg, decl_local_2arg, decl_local_1arg)(__VA_ARGS__)

#define decl_local_array(name, ptr)                                                 \
    decl_local(name, ({                                                             \
        auto *__ptr = new std::remove_reference_t<decltype(name)>{};                \
        *__ptr = ptr;                                                               \
        __ptr;                                                                      \
    }), [] (void *p_) {                                                             \
        auto p = reinterpret_cast<std::remove_reference_t<decltype(name)> *>(p_);   \
        delete[] *p;                                                                \
        delete p;                                                                   \
    })

#endif
