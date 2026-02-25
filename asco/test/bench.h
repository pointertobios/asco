// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <print>
#include <string_view>
#include <vector>

namespace asco::test {

using span_head = std::chrono::steady_clock::time_point;

class bench_context {
public:
    bench_context(std::string_view name, std::size_t warmup, std::size_t measure)
            : m_name{name}
            , m_warmup_count{warmup}
            , m_measure_count{measure}
            , m_measurements{} {
        m_measurements.reserve(measure);
    }

    span_head get_span() noexcept { return std::chrono::steady_clock::now(); }

    bool commit(span_head head) noexcept {
        if (m_commit_count >= m_warmup_count && m_commit_count < m_warmup_count + m_measure_count) {
            const auto tail = std::chrono::steady_clock::now();
            const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(tail - head);
            m_measurements.push_back(dur);
        }
        return ++m_commit_count == m_warmup_count + m_measure_count;
    }

    ~bench_context() {
        using namespace std::chrono_literals;
        if (m_measurements.empty()) {
            std::println("{}: (no measurements)", m_name);
            return;
        }

        std::sort(m_measurements.begin(), m_measurements.end());
        auto total = 0ns;
        auto max = 0ns;
        for (const auto &dur : m_measurements) {
            total += dur;
            if (dur > max) {
                max = dur;
            }
        }
        const auto n = m_measurements.size();
        const auto avg = total / n;
        const auto p50 = m_measurements[n / 2];
        const auto p90 = m_measurements[n * 9 / 10];
        const auto p99 = m_measurements[n * 99 / 100];
        const auto p999 = m_measurements[n * 999 / 1000];
        std::println(
            "{}: avg = {}, max = {}, p50 = {}, p90 = {}, p99 = {}, p999 = {}",  //
            m_name, avg, max, p50, p90, p99, p999);
    }

private:
    const std::string_view m_name;
    const std::size_t m_warmup_count;
    const std::size_t m_measure_count;
    std::size_t m_commit_count{0};
    std::vector<std::chrono::steady_clock::duration> m_measurements;
};

};  // namespace asco::test
