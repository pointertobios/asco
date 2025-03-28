#include <asco/utils/channel.h>
#include <iostream>
#include <thread>

using std::jthread;

int main() {
    auto [tx, rx] = asco_inner::channel<int>(128);
    jthread t([&rx]{
        while (true) if (auto i = rx.recv(); i) {
            std::cout << *i << std::endl;
        } else break;
    });
    for (int i = 0; i < 30000; ++i) {
        tx.send(i);
    }
    tx.stop();
    return 0;
}
