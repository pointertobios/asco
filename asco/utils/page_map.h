#ifndef PAGEMAP_H
#define PAGEMAP_H 1

#include <functional>

namespace asco {
inline std::function<void *(size_t)> alloc_func = [](size_t size) { return ::operator new(size); };

template<int BITS>
class pagemap3 {
private:
    // How many bits should we consume at each interior level
    static const int interior_bits = (BITS + 2) / 3;  // Round-up
    static const int interior_length = 1 << interior_bits;

    // How many bits should we consume at leaf level
    static const int leaf_bits = BITS - 2 * interior_bits;
    static const int LEAF_LENGTH = 1 << leaf_bits;

    // Interior node
    struct node {
        node *ptrs[interior_length];
    };

    // Leaf node
    struct leaf {
        void *values[LEAF_LENGTH];
    };

    node *root_;                               // Root of radix tree
    std::function<void *(size_t)> allocator_;  // Memory allocator

    node *new_node() {
        node *result = reinterpret_cast<node *>(allocator_(sizeof(node)));
        if (result != NULL) {
            memset(result, 0, sizeof(*result));
        }
        return result;
    }

public:
    using number = uintptr_t;

    explicit pagemap3(std::function<void *(size_t)> allocator = alloc_func) {
        allocator_ = allocator;
        root_ = new_node();
    }

    void *get(number k) const {
        const number i1 = k >> (leaf_bits + interior_bits);
        const number i2 = (k >> leaf_bits) & (interior_length - 1);
        const number i3 = k & (LEAF_LENGTH - 1);
        if ((k >> BITS) > 0 || root_->ptrs[i1] == NULL || root_->ptrs[i1]->ptrs[i2] == NULL) {
            return NULL;
        }
        return reinterpret_cast<leaf *>(root_->ptrs[i1]->ptrs[i2])->values[i3];
    }

    void set(number k, void *v) {
        assert(k >> BITS == 0);
        const number i1 = k >> (leaf_bits + interior_bits);
        const number i2 = (k >> leaf_bits) & (interior_length - 1);
        const number i3 = k & (LEAF_LENGTH - 1);
        reinterpret_cast<leaf *>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
    }

    void ensure_set(number k, void *v) {
        ensure(k, 1);
        set(k, v);
    }

    bool ensure(number start, size_t n) {
        for (number key = start; key <= start + n - 1;) {
            const number i1 = key >> (leaf_bits + interior_bits);
            const number i2 = (key >> leaf_bits) & (interior_length - 1);

            // Check for overflow
            if (i1 >= interior_length || i2 >= interior_length)
                assert(false);

            // Make 2nd level node if necessary
            if (root_->ptrs[i1] == NULL) {
                node *n = new_node();
                if (n == NULL)
                    return false;
                root_->ptrs[i1] = n;
            }

            // Make leaf node if necessary
            if (root_->ptrs[i1]->ptrs[i2] == NULL) {
                leaf *leaf = reinterpret_cast<leaf *>(allocator_(sizeof(leaf)));
                if (leaf == NULL)
                    return false;
                memset(leaf, 0, sizeof(*leaf));
                root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<node *>(leaf);
            }

            // Advance key past whatever is covered by this leaf node
            key = ((key >> leaf_bits) + 1) << leaf_bits;
        }
        return true;
    }
};
}  // namespace asco

#endif