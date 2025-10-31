// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/panic/unwind.h>

#include <asco/core/runtime.h>
#include <asco/panic/color.h>

namespace asco::panic {

static std::string
format_stack_entry(size_t i, const cpptrace::stacktrace_frame &e, bool color, bool is_coroutine = false) {
    std::string res;
    res.reserve(256);

    if (color) {
        res += number_color;
    }
    res += "·";
    res += std::to_string(i);
    if (color) {
        res += reset_color;
    }
    res += " ";

    if (color) {
        res += address_color;
    }
    if (e.is_inline)
        res += "       (inlined)";
    else
        res += std::format("{:#016x}", reinterpret_cast<uintptr_t>(e.raw_address));
    if (color) {
        res += reset_color;
    }
    res += " in ";

    if (color) {
        res += is_coroutine ? co_name_color : name_color;
    }
    res += e.symbol.ends_with("[clone .resume]")  ? e.symbol.substr(0, e.symbol.size() - 16)
           : e.symbol.ends_with("[clone .actor]") ? e.symbol.substr(0, e.symbol.size() - 15)
           : e.symbol.ends_with("(.resume)")      ? e.symbol.substr(0, e.symbol.size() - 10)
           : e.symbol.ends_with(".resume")        ? e.symbol.substr(0, e.symbol.size() - 8)
                                                  : e.symbol;
    if (color) {
        res += reset_color;
    }

    res += "\n  at ";
    if (!e.filename.empty()) {
        std::string_view name = e.filename;
        std::vector<std::string_view> path;
        size_t i{0};
        while (i != name.size()) {
            if (name[i] != '/') {
                i++;
                if (i == name.size())
                    path.push_back(std::move(name));
                continue;
            }
            auto sec = std::string_view{name.begin(), name.begin() + i};
            if (sec == "..") {
                if (!path.empty())
                    path.pop_back();
            } else if (sec != ".") {
                path.push_back(sec);
            }
            name = name.substr(i + 1);
            i = 0;
        }
        if (color) {
            res += file_color;
        }
        for (const auto &seg : path) {
            if (color && seg == path.back()) {
                res += file_tail_color;
            }
            res += seg;
            if (seg != path.back())
                res += '/';
        }
        if (color) {
            res += reset_color;
        }

        if (e.line.has_value() && e.line.value()) {
            res += ":";
            if (color) {
                res += lineno_color;
            }
            res += std::to_string(e.line.value());
            if (color) {
                res += reset_color;
            }
            if (e.column.has_value() && e.column.value()) {
                res += ":";
                if (color) {
                    res += lineno_color;
                }
                res += std::to_string(e.column.value());
                if (color) {
                    res += reset_color;
                }
            }
        }
    } else {
        if (color) {
            res += file_tail_color;
        }
        res += "??";
        if (color) {
            res += reset_color;
        }
    }

    res += "\n";

    return res;
}

std::string unwind(size_t skip, bool color) {
    std::string res;
    res.reserve(4096);

    auto st = cpptrace::stacktrace::current(skip);
    size_t i{0};
    for (auto &e : st) {
        res += format_stack_entry(i, e, color);
        i++;
    }

    return res;
}

std::string co_unwind(size_t skip, bool color) {
    std::string res;
    res.reserve(4096);

    auto st = cpptrace::stacktrace::current(skip);
    size_t i{0};
    for (auto &e : st) {
        bool is_coroutine = e.symbol.ends_with("[clone .resume]") || e.symbol.ends_with("[clone .actor]")
                            || e.symbol.ends_with("(.resume)") || e.symbol.ends_with(".resume");
        res += format_stack_entry(i, e, color, is_coroutine);
        i++;
        if (is_coroutine)
            break;
    }

    auto task = core::runtime::this_runtime().get_task_by(core::worker::this_worker().current_task());
    for (; task; task = task->caller) {
        if (!task->caller) {
            for (const auto &e : task->raw_stacktrace) {
                res += format_stack_entry(i, e, color);
                i++;
            }
            break;
        }

        res += format_stack_entry(i, *task->caller_coroutine_trace, color, true);
        i++;
    }

    return res;
}

};  // namespace asco::panic
