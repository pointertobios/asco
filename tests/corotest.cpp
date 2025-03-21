#include <asco/runtime.h>
#include <asco/future.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>

using asco::future;

future<int> foo() {
    co_return 3;
}

int main()
{
    auto i = foo();
    return 0;
}
