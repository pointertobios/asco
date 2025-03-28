#include <iostream>

#include <asco/future.h>

using asco::future, asco::future_void;

future<int> foo(int i) {
    std::cout << "foo" << std::endl;
    co_return 3 * i;
}

asco_main future<int> async_main() {
    std::cout << "async_main" << std::endl;
    int s = 0;
    for (int i = 0; i < 10000; i++)
        s += co_await foo(i);
    std::cout << s << std::endl;
    co_return 0;
}
