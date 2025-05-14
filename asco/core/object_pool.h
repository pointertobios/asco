#ifndef ASCO_OBJECT_POLL_H
#define ASCO_OBJECT_POLL_H 1

#include <asco/sync/spin.h>
#include <asco/utils/page_map.h>
#include <unordered_map>
#include <vector>

// namespace asco {
// const size_t align_num = 16;
// const size_t alloc_space = 1024 * 1024;

// template<class T>
// class object_pool {
// public:
//     static object_pool &get_pool() { return _object_pool; }

//     void *alloc(size_t n) {
//         // 获取对应的memory内存块
//         auto guard = freelists.lock();
//         if (guard->find(n) == guard->end())
//             guard->emplace(n, nullptr);
//         auto &freelist = (guard->find(n))->second;

//         void *ret = nullptr;
//         if (freelist != nullptr) {
//             // 如果回收站有空间，直接将内存块从回收站中取出使用
//             ret = (std::byte *)freelist
//                   + align_num;  // 前16byte的空间用于存放地址（防止用户的内存块多占用缓冲行）
//             freelist = *(void **)freelist;
//             return ret;
//         }
//         // 计算实际需要的内存大小
//         size_t obj_len = align_num + align_byte(n);
//         // 若回收站没有空间，则从memory中取空间构造
//         auto mem_guard = memory.lock();
//         if (leftMemLen < obj_len) {
//             // 如果memory中空间不够用了，则重新开辟一块空间
//             leftMemLen = alloc_space;
//             *mem_guard = (std::byte *)(::operator new(alloc_space));
//             if (!*mem_guard) {
//                 throw std::bad_alloc();
//             }
//         }
//         ret = (std::byte *)(*mem_guard) + align_num;
//         // 保证可以存下单个地址长度以便建立链表
//         (*mem_guard) += obj_len;
//         leftMemLen -= obj_len;
//         return ret;
//     }

//     void dealloc(void *obj, size_t n) {
//         // 获取对应的自由链表
//         auto freelist = freelists.lock()->find(n)->second;
//         // 头插法将freelist的地址存放在obj首个地址长度当中
//         void *actual_addr = (std::byte *)obj - align_num;
//         *(void **)(actual_addr) = freelist;
//         freelist = actual_addr;
//     }

//     // 最终释放内存
//     ~object_pool() {}

// private:
//     object_pool() {}
//     size_t align_byte(size_t n) {
//         return (n + align_num - 1) & ~(align_num - 1);  // 内存对齐至16byte
//     }
//     object_pool(const object_pool &) = delete;
//     object_pool &operator=(const object_pool &) = delete;
//     object_pool(object_pool &&) = delete;
//     object_pool &operator=(object_pool &&) = delete;

// private:
//     static object_pool _object_pool;
//     // std::byte *memory = nullptr;
//     size_t leftMemLen = 0;
//     // void *freelist = nullptr;
//     spin<std::byte *> memory;
//     spin<std::unordered_map<size_t, void *>> freelists;
// };

// template<typename T>
// inline object_pool<T> object_pool<T>::_object_pool;
// }  // namespace asco

#include <array>
#include <mutex>
#include <vector>
#include <memory>
#include <thread>

namespace asco {
constexpr size_t align_num = 16;
constexpr size_t kMaxSmallSize = 256;
constexpr size_t kNumClasses = kMaxSmallSize / align_num;
constexpr size_t kBatchAllocSize = 1024 * 1024;

template <class T>
class object_pool {
    struct memory_block {
        char* ptr;
        size_t size;
    };

public:
    static object_pool& get_pool() {
        static object_pool instance;
        return instance;
    }

    void* alloc(size_t size) {
        if (size > kMaxSmallSize) {
            return alloc_large(size);
        }

        // 1. 尝试从线程本地缓存分配
        size_t class_id = size_to_class_id(size);
        if (tls_freelist_[class_id]) {
            void* obj = tls_freelist_[class_id];
            tls_freelist_[class_id] = *(void**)obj;
            return obj;
        }

        // 2. 从全局池批量补充
        return refill(class_id, class_id_to_size(class_id));
    }

    void dealloc(void* obj, size_t size) {
        if (size > kMaxSmallSize) {
            dealloc_large(obj, size);
            return;
        }

        // 回收至线程本地缓存
        size_t class_id = size_to_class_id(size);
        *(void**)obj = tls_freelist_[class_id];
        tls_freelist_[class_id] = obj;
    }

private:
    // 大小分级转换
    static size_t size_to_class_id(size_t size) {
        return (size + align_num - 1) / align_num;
    }
    static size_t class_id_to_size(size_t class_id) {
        return class_id * align_num;
    }

    // 从全局池补充本地缓存
    void* refill(size_t class_id, size_t size) {
        constexpr int kBatchSize = 32;  // 每次补充32个对象
        
        std::lock_guard<std::mutex> lock(mutex_);
        char* chunk = alloc_chunk(size * kBatchSize);
        
        // 构建自由链表
        for (int i = 0; i < kBatchSize - 1; ++i) {
            void** next = reinterpret_cast<void**>(chunk + i * size);
            *next = chunk + (i + 1) * size;
        }
        
        *(void**)(chunk + (kBatchSize - 1) * size) = nullptr;
        tls_freelist_[class_id] = chunk + size;  // 返回第一个对象
        return chunk;
    }

    // 大对象直接分配
    void* alloc_large(size_t size) {
        return ::operator new(size);
    }
    void dealloc_large(void* obj, size_t size) {
        ::operator delete(obj);
    }

    // 分配连续内存块
    char* alloc_chunk(size_t size) {
        if (current_block_.size < size) {
            current_block_.ptr = static_cast<char*>(::operator new(kBatchAllocSize));
            current_block_.size = kBatchAllocSize;
            blocks_.push_back(current_block_.ptr);
        }
        
        char* result = current_block_.ptr;
        current_block_.ptr += size;
        current_block_.size -= size;
        return result;
    }

    // 线程本地缓存
    inline thread_local static std::array<void*, kNumClasses> tls_freelist_;

    // 全局内存块管理
    memory_block current_block_;
    std::vector<void*> blocks_;
    std::mutex mutex_;
};

}  // namespace asco


#endif