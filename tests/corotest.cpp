#include <asco/runtime.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>

int main()
{
    asco::runtime rt(1);
    // auto f = rt.spawn<int>([]{
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    //     return 3;
    // });
    // rt.block_on<asco::future_void>([]{
    //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
    //     return asco::future_void{};
    // });
    // auto f2 = rt.spawn_blocking<std::vector<int>>([]{
    //     std::vector<int> v;
    //     for (int i = 0; i < 100; ++i)
    //         v.push_back(i);
    //     return v;
    // });
    // std::cout << f->await() << std::endl;
    // for (auto i : f2->await()) {
    //     std::cout << i;
    // }
    // std::cout << std::endl;
    return 0;
}
