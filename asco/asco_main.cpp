#include <asco/future.h>

#include <functional>
#include <optional>

using asco::future;

extern future<int> async_main();

inline asco::runtime_initializer_t runtime_initializer;

int main(int argc, const char **argv, const char **env) {
    asco::runtime *rt;
    if (runtime_initializer) {
        rt = (*runtime_initializer)();
    } else {
        rt = new asco::runtime();
    }
    asco::runtime::sys::set_args(argc, argv);
    asco::runtime::sys::set_env(const_cast<char **>(env));
    return async_main().await();
}
