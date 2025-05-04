# 通道

导入 `<asco/channel.h>` 头文件使用。

通道与管道类似，但是通道完全由用户态代码实现，支持在 asco 运行时中阻塞读取。

通道分为三部分：管道本体、接收端和发送端。管道本体是无法被访问的。用户只需使用接收端和发送端即可。

## 发送端（ `asco::sender<T>` ）

* 构造函数： 仅有移动构造函数
* 赋值运算符： 仅有移动赋值运算符
* `std::optional<T> send(T &&)`：移动发送，若接收端关闭，将传入的值返回，否则返回 `std::nullopt`
* `std::optional<T> send(T &)`：拷贝发送
* `void stop()`：关闭发送端，接收端将无法继续接收数据，析构时自动调用此函数

## 接收端（ `asco::receiver<T>` ）

* 构造函数： 仅有移动构造函数
* 赋值运算符： 仅有移动赋值运算符
* `future_inline<std::optional<T>> recv()`：***可打断***的***异步***接收，若发送端关闭且通道中没有更多对象，返回 `std::nullopt`
* `std::expected<T, receive_fail> try_recv()`: 尝试接收一个对象，通道中没有新对象返回 `std::unexpected(receive_fail::non_object)`
    ，通道关闭返回 `std::unexpected(receive_fail::closed)`
* `bool is_stopped()`：判断通道是否关闭
* `void stop()`：关闭接收端，发送端将无法继续发送数据，析构时自动调用此函数

## 单生产者单消费者通道（ `asco::ss::channel` ）

单生产者单消费者通道使用工具函数 `std::pair<sender<T>, receiver<T>> ss::channel<T>()` 创建。

此通道仅支持一个协程访问发送端，一个协程访问接收端，两个端没有并发安全特性。

```c++
#include <asco/channel.h>

future_void sending(asco::sender<int> tx) {
    for (int i = 0; i < 30000; i++) {
        auto r = tx.send(i);
        if (r.has_value())
            break;
    }
    co_return {};
}

auto [tx, rx] = asco::ss::channel<int>();
sending(std::move(tx));
while (true) if (auto r = co_await rx.recv(); r) {
    std::cout << *r << std::endl;
} else break;
```

`tx` 是发送端， `rx` 是接收端。
