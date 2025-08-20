// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_PERF_H
#define ASCO_PERF_H 1

#include <chrono>
#include <unordered_map>
#include <vector>

#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>
#include <asco/utils/type_hash.h>

namespace asco::perf {

using duration = std::chrono::high_resolution_clock::duration;
using time_point = std::chrono::high_resolution_clock::time_point;
using namespace std::chrono_literals;

struct record {
    const char *coro_name;
    duration total;
    duration active;

    size_t counter{1};

    bool operator<(const record &rhs) const {
        if (active != rhs.active)
            return active > rhs.active;
        if (total != rhs.total)
            return total > rhs.total;
        if (counter != rhs.counter)
            return counter > rhs.counter;
        std::string_view a{coro_name}, b{rhs.coro_name};
        return a < b;
    }

    inline static spin<std::unordered_map<size_t, record>> global_record;
    inline static std::unordered_map<size_t, record> &&collect() { return global_record.get(); }
};

class coro_recorder {
public:
    coro_recorder() = default;

    ~coro_recorder() {
        if (!coro_name)
            return;

        auto now = std::chrono::high_resolution_clock::now();
        duration total{now - start_time};
        duration active{0ns};
        if (awake_suspend_queue.size() % 2)
            awake_suspend_queue.push_back(now);
        for (size_t i{0}; i < awake_suspend_queue.size(); i += 2) {
            active += awake_suspend_queue[i + 1] - awake_suspend_queue[i];
        }

        auto guard = record::global_record.lock();
        if (auto it = guard->find(coro_hash); it != guard->end()) {
            auto &r = it->second;
            r.total += total;
            r.active += active;
            r.counter++;
        } else {
            guard->emplace(coro_hash, record{coro_name, total, active});
        }
    }

    template<size_t Hash>
    void enable_recorder(const char *name) {
        coro_hash = Hash;
        coro_name = name;
    }

    inline void record_once() { awake_suspend_queue.push_back(std::chrono::high_resolution_clock::now()); }

private:
    size_t coro_hash{0};
    const char *coro_name{nullptr};

    time_point start_time{std::chrono::high_resolution_clock::now()};
    // This queue always starts with an awake action time.
    // And unless at select block, awake and suspend actions are always in pairs.
    // If the coroutin is at a select block, fill a now time point in destructor.
    std::vector<time_point> awake_suspend_queue;
};

class func_recorder {
public:
    func_recorder(size_t coro_hash, const char *coro_name)
            : coro_hash(coro_hash)
            , coro_name(coro_name) {}

    ~func_recorder() {
        duration total{std::chrono::high_resolution_clock::now() - start_time};
        auto guard = record::global_record.lock();
        if (guard->contains(coro_hash)) {
            auto &r = guard->at(coro_hash);
            r.total += total;
            r.active += total;
            r.counter++;
        } else {
            guard->emplace(coro_hash, record{coro_name, total, total});
        }
    }

private:
    size_t coro_hash;
    const char *coro_name;

    time_point start_time{std::chrono::high_resolution_clock::now()};
};

};  // namespace asco::perf

#ifdef ASCO_PERF_RECORD
#    if defined(__clang__) || defined(__GNUC__)
#        define coro_perf()                                                                       \
            RT::__worker::get_worker()                                                            \
                .current_task()                                                                   \
                .perf_recorder->enable_recorder<asco::__consteval_str_hash(__PRETTY_FUNCTION__)>( \
                    __PRETTY_FUNCTION__)

#        define func_perf() \
            asco::perf::func_recorder(asco::__consteval_str_hash(__PRETTY_FUNCTION__), __PRETTY_FUNCTION__)
#    else
#        define coro_perf()            \
            RT::__worker::get_worker() \
                .current_task()        \
                .perf_recorder->enable_recorder<asco::__consteval_str_hash(__FUNCSIG__)>(__FUNCSIG__)

#        define func_perf() asco::perf::func_recorder(asco::__consteval_str_hash(__FUNCSIG__), __FUNCSIG__)
#    endif
#else
#    define coro_perf()
#    define func_perf() 0
#endif

#endif
