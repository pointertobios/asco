// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UNWIND_H
#define ASCO_UNWIND_H 1

#include <format>
#include <sstream>
#include <string>
#include <vector>

#ifdef __linux__
#    include <elfutils/libdwfl.h>
#else
#    error "Unwinder unimplemented for this platform"
#endif

#include <asco/utils/pubusing.h>

namespace asco::unwind {

void *unwind_index(size_t index);

struct stack {
    void *addr;
    std::string name;
    std::vector<std::string> path;
    int line;
};

struct stacktrace {
    std::vector<stack> stackv;
};

class function_unwinder {
public:
    function_unwinder();
    ~function_unwinder();

    function_unwinder(const function_unwinder &) = delete;
    function_unwinder(function_unwinder &&) = delete;

    function_unwinder &operator=(const function_unwinder &) = delete;
    function_unwinder &operator=(function_unwinder &&) = delete;

    stack resolve_address(size_t addr);

    static stacktrace unwind();

private:
    struct linux_unwind_context {
#ifdef __linux__
        ::Dwfl_Callbacks callbacks;
        ::Dwfl *dwfl;
        char *debuginfo_path;
#endif
    };

    union os_unwind_context {
        linux_unwind_context linux_;
    } os;

protected:
    std::vector<stack> stacks;
};

struct coro_trace {
    void *current;
    coro_trace *prev;
};

};  // namespace asco::unwind

template<>
struct std::formatter<asco::unwind::stacktrace, char> {
    bool color = false;
    constexpr auto parse(std::format_parse_context &ctx) {
        auto it = ctx.begin();
        auto end = ctx.end();
        if (it != end && *it == 'c') {
            color = true;
            ++it;
        }
        return it;
    }

    const char *number_color{"\033[1;35m"};
    const char *address_color{"\033[1;34m"};
    const char *name_color{"\033[32m"};
    const char *file_color{"\033[33m"};
    const char *file_tail_color{"\033[1;33m"};
    const char *lineno_color{"\033[1;34m"};

    template<typename FormatContext>
    auto format(const asco::unwind::stacktrace &trace, FormatContext &ctx) const {
        std::ostringstream oss;
        size_t index{0};
        for (auto &frame : trace.stackv) {
            if (color)
                oss << number_color << "·" << index << "\033[0m ";
            else
                oss << "·" << index << " ";

            if (color) {
                if (frame.line != -1)
                    oss << address_color;
            }
            oss << std::format("{:#016x}", (uintptr_t)frame.addr) << "\033[0m ";
            if (color)
                oss << name_color;
            oss << frame.name << "\033[0m";

            oss << "\n  at ";
            for (size_t i{0}, size{frame.path.size()}; i < size; i++) {
                if (color) {
                    if (i + 1 == size)
                        oss << file_tail_color << frame.path[i] << "\033[0m";
                    else
                        oss << file_color << frame.path[i] << "/";
                } else {
                    if (i + 1 == size)
                        oss << frame.path[i];
                    else
                        oss << frame.path[i] << "/";
                }
            }
            if (frame.line != -1) {
                if (color)
                    oss << ":" << lineno_color << frame.line << "\033[0m";
                else
                    oss << ":" << frame.line;
            }

            oss << "\n\n";
            index++;
        }
        return format_to(ctx.out(), "{}", oss.str());
    }
};

#endif
