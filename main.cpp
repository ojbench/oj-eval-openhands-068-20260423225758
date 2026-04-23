

#include <iostream>
#include <vector>
#include <map>
#include "allocator.hpp"

int main() {
    std::size_t poolSize;
    if (!(std::cin >> poolSize)) return 0;
    
    TLSFAllocator allocator(poolSize);
    
    int numOps;
    if (!(std::cin >> numOps)) return 0;
    
    std::map<std::size_t, void*> offsetToPtr;
    
    for (int i = 0; i < numOps; ++i) {
        int op;
        if (!(std::cin >> op)) break;
        if (op == 1) {
            std::size_t size;
            std::cin >> size;
            void* ptr = allocator.allocate(size);
            if (ptr) {
                std::size_t offset = reinterpret_cast<char*>(ptr) - reinterpret_cast<char*>(allocator.getMemoryPoolStart());
                std::cout << offset << std::endl;
                offsetToPtr[offset] = ptr;
            } else {
                std::cout << -1 << std::endl;
            }
        } else if (op == 2) {
            std::size_t offset;
            std::cin >> offset;
            if (offsetToPtr.count(offset)) {
                allocator.deallocate(offsetToPtr[offset]);
                offsetToPtr.erase(offset);
            }
        } else if (op == 3) {
            std::cout << allocator.getMaxAvailableBlockSize() << std::endl;
        }
    }
    
    return 0;
}

