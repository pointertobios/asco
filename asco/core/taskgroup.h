// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_TASKGROUP_H
#define ASCO_TASKGROUP_H 1

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asco/core/sched.h>
#include <asco/utils/dynvar.h>
#include <asco/utils/pubusing.h>
#include <asco/utils/type_hash.h>

namespace asco {

class task_group {
public:
    inline task_group() {}

    inline void add_task(sched::task::task_id id) { tasks.insert(id); }

    template<size_t Hash>
    bool var_exists() {
        return vars.contains(Hash);
    }

    template<typename T, size_t Hash>
    T &get_var(const char *name) {
        if (auto it = vars.find(Hash); it == vars.end())
            throw std::runtime_error(std::format("[ASCO] Task group local variable \'{}\' not found", name));

        if (vars[Hash].type != type_hash<T>())
            throw std::runtime_error(
                std::format("[ASCO] Task group local variable \'{}\' type mismatch", name));
        return *reinterpret_cast<T *>(vars[Hash].p);
    }

    template<typename T, size_t Hash>
    T &decl_var(const char *name, T *pt, dynvar::destructor destructor) {
        if (auto it = vars.find(Hash); it != vars.end())
            throw std::runtime_error(
                std::format("[ASCO] Task group local variable \'{}\' already declared", name));

        vars[Hash] = dynvar{type_hash<T>(), pt, destructor};
        return *pt;
    }

    template<typename T, size_t Hash>
    T &decl_var(const char *name, T *pt) {
        return decl_var<T, Hash>(name, pt, [](void *p) { delete reinterpret_cast<T *>(p); });
    }

    template<typename T, size_t Hash>
    T &decl_var(const char *name) {
        return decl_var<T, Hash>(name, new T);
    }

    inline void del_var(size_t hash) {
        if (auto it = vars.find(hash); it != vars.end()) {
            it->second.deconstruct(it->second.p);
            vars.erase(it);
        }
    }

private:
    std::unordered_set<sched::task::task_id> tasks;

    std::unordered_map<size_t, dynvar> vars;
};

};  // namespace asco

#define group_local(name)                                              \
    &name = RT::get_runtime()                                          \
                ->group(RT::__worker::get_worker()->current_task_id()) \
                ->get_var<std::remove_reference_t<decltype(name)>, asco::__consteval_str_hash(#name)>(#name)

#define decl_glocal_1arg(name)                                     \
    &name =                                                        \
        RT::get_runtime()                                          \
            ->group(RT::__worker::get_worker()->current_task_id()) \
            ->decl_var<std::remove_reference_t<decltype(name)>, asco::__consteval_str_hash(#name)>(#name)

#define decl_glocal_2arg(name, ptr)                                                                     \
    &name = RT::get_runtime()                                                                           \
                ->group(RT::__worker::get_worker()->current_task_id())                                  \
                ->decl_var<std::remove_reference_t<decltype(name)>, asco::__consteval_str_hash(#name)>( \
                    #name, ptr)

#define decl_glocal_3arg(name, ptr, destructor)                                                         \
    &name = RT::get_runtime()                                                                           \
                ->group(RT::__worker::get_worker()->current_task_id())                                  \
                ->decl_var<std::remove_reference_t<decltype(name)>, asco::__consteval_str_hash(#name)>( \
                    #name, ptr, destructor)

#define decl_glocal(...) \
    __dispatch(__VA_ARGS__, decl_glocal_3arg, decl_glocal_2arg, decl_glocal_1arg)(__VA_ARGS__)

#define del_glocal(name)                                       \
    RT::get_runtime()                                          \
        ->group(RT::__worker::get_worker()->current_task_id()) \
        ->del_var(asco::__consteval_str_hash(name))

#endif
