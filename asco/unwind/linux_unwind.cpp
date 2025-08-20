// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/unwind/unwind.h>

#include <dwarf.h>
#include <ranges>
#include <unistd.h>

namespace asco::unwind {

function_unwinder::function_unwinder() {
    os.linux_.callbacks = {
        .find_elf = ::dwfl_linux_proc_find_elf,
        .find_debuginfo = ::dwfl_standard_find_debuginfo,
        .section_address = nullptr,
        .debuginfo_path = &os.linux_.debuginfo_path,
    };
    os.linux_.dwfl = ::dwfl_begin(&os.linux_.callbacks);
    ::dwfl_linux_proc_report(os.linux_.dwfl, ::getpid());
    ::dwfl_report_end(os.linux_.dwfl, nullptr, nullptr);
}

function_unwinder::~function_unwinder() { ::dwfl_end(os.linux_.dwfl); }

stack function_unwinder::resolve_address(size_t addr_) {
    Dwfl *dwfl = os.linux_.dwfl;
    Dwarf_Addr addr = addr_;
    ::Dwfl_Module *mod = ::dwfl_addrmodule(dwfl, addr);

    ::Dwarf_Addr bias = 0;
    ::dwfl_module_info(mod, nullptr, nullptr, nullptr, &bias, nullptr, nullptr, nullptr);

    const char *symname = ::dwfl_module_addrname(mod, addr);
    if (!symname)
        symname = "??";
    const char *name{nullptr};
    name = (symname ? inner::demangle(symname) : "??");

    const char *src_file = "??";
    int line_num = -1;
    ::Dwfl_Line *line_ = ::dwfl_module_getsrc(mod, addr);
    if (line_) {
        ::Dwarf_Line *line = ::dwfl_dwarf_line(line_, &bias);
        ::Dwarf_Addr line_addr;
        ::dwarf_lineaddr(line, &line_addr);
        src_file = ::dwarf_linesrc(line, nullptr, nullptr);
        ::dwarf_lineno(line, &line_num);
    }
    std::vector<std::string> path;
    std::string_view src_file_view(src_file);
    for (auto chunk : src_file_view | std::views::split('/')) {
        std::string str(chunk.begin(), chunk.end());
        if (str == ".." && !path.empty()) {
            path.pop_back();
        } else {
            path.emplace_back(str);
        }
    }

    return {
        .addr = (void *)addr,
        .name = name,
        .path = path,
        .line = line_num,
    };
}

};  // namespace asco::unwind
