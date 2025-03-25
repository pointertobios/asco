#include <iostream>

#include <asco/future.h>

using asco::future, asco::future_void;

future<int> foo() {
    std::cout << "foo" << std::endl;
    co_return 3;
}

future_void async_main() {
    std::cout << "async_main" << std::endl;
    try {
        std::cout << co_await foo() << std::endl;
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
    co_return {};
}

int main()
{
    asco::runtime rt;
    async_main().await();
    return 0;
}
