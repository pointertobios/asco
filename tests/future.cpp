#include <print>

#include <asco/core/runtime.h>
#include <asco/future.h>

using namespace asco;
using namespace std::literals::chrono_literals;

future<int> int_func() {
    std::println("int_func()");
    co_return 42;
}

future<void> void_func() {
    std::println("void_func()");
    co_return;
}

int main() {
    core::runtime rt;
    auto hdl = rt.spawn(void_func());
    rt.block_on([&hdl] -> future<void> { co_await hdl; });
    return rt.block_on(int_func());
}
