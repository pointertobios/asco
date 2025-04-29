#include <cxxabi.h>
#include <functional>
#include <iostream>
#include <optional>
#include <typeinfo>

#include <asco/future.h>
#include <asco/lazy_delete.h>

using asco::future, asco::lazy_delete;
using asco::runtime;

extern future<int> async_main();

inline asco::runtime_initializer_t runtime_initializer;

int main(int argc, const char **argv, const char **env) {
    lazy_delete ld{({
        runtime *rt;
        if (runtime_initializer) {
            rt = (*runtime_initializer)();
        } else {
            rt = new runtime();
        }
        rt;
    })};
    runtime::sys::set_args(argc, argv);
    runtime::sys::set_env(const_cast<char **>(env));
    try {
        return async_main().await();
    } catch (std::exception &e) {
        int t;
        std::cerr << std::format(
            "[asco-main] async main throw \'{}\'", abi::__cxa_demangle(typeid(e).name(), 0, 0, &t))
                  << std::endl
                  << std::format("  what():  {}", e.what()) << std::endl;
        return -1;
    }
}
