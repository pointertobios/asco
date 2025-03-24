#include <iostream>

#include <asco/runtime.h>
#include <asco/future.h>

using asco::future;

future<int> foo() {
    std::cout << "foo" << std::endl;
    co_return 3;
}

int main()
{
    asco::runtime rt;
    auto i = foo();
    return 0;
}
