
#include "allocator.hpp"
#include <cstdlib>
#include <algorithm>

static int fls(std::size_t x) {
    if (x == 0) return -1;
    return 63 - __builtin_clzll(x);
}

static int ffs(std::uint32_t x) {
    if (x == 0) return -1;
    return __builtin_ctz(x);
}

TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) {
    initializeMemoryPool(memoryPoolSize);
}

TLSFAllocator::~TLSFAllocator() {
    if (memoryPool) {
        free(memoryPool);
    }
}

void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    poolSize = size;
    memoryPool = malloc(size);
    
    index.fliBitmap = 0;
    for (int i = 0; i < FLI_SIZE; ++i) {
        index.sliBitmaps[i] = 0;
        for (int j = 0; j < SLI_SIZE; ++j) {
            index.freeLists[i][j] = nullptr;
        }
    }
    
    FreeBlock* initialBlock = static_cast<FreeBlock*>(memoryPool);
    initialBlock->size = size;
    initialBlock->isFree = true;
    initialBlock->data = reinterpret_cast<char*>(initialBlock) + sizeof(BlockHeader);
    initialBlock->prevPhysBlock = nullptr;
    initialBlock->nextPhysBlock = nullptr;
    initialBlock->prevFree = nullptr;
    initialBlock->nextFree = nullptr;
    
    insertFreeBlock(initialBlock);
}

void* TLSFAllocator::allocate(std::size_t size) {
    if (size == 0) return nullptr;
    
    std::size_t adjustSize = size + sizeof(BlockHeader);
    if (adjustSize < sizeof(FreeBlock)) adjustSize = sizeof(FreeBlock);
    
    // Round up to the next bin
    std::size_t roundedSize = adjustSize;
    int fli_tmp = fls(adjustSize);
    if (fli_tmp >= SLI_BITS) {
        std::size_t bin_width = 1ULL << (fli_tmp - SLI_BITS);
        roundedSize = (adjustSize + bin_width - 1) & ~(bin_width - 1);
    }
    
    FreeBlock* block = findSuitableBlock(roundedSize);
    if (!block) return nullptr;
    
    removeFreeBlock(block);
    splitBlock(block, adjustSize);
    
    block->isFree = false;
    return block->data;
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    BlockHeader* header = reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) - sizeof(BlockHeader));
    header->isFree = true;
    
    FreeBlock* freeBlock = static_cast<FreeBlock*>(header);
    freeBlock->prevFree = nullptr;
    freeBlock->nextFree = nullptr;
    
    mergeAdjacentFreeBlocks(freeBlock);
}

void* TLSFAllocator::getMemoryPoolStart() const {
    return memoryPool;
}

std::size_t TLSFAllocator::getMemoryPoolSize() const {
    return poolSize;
}

std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    if (index.fliBitmap == 0) return 0;
    int fli = 31 - __builtin_clz(index.fliBitmap);
    int sli = 31 - __builtin_clz(index.sliBitmaps[fli]);
    FreeBlock* block = index.freeLists[fli][sli];
    
    std::size_t maxSize = 0;
    while (block) {
        if (block->size > maxSize) maxSize = block->size;
        block = block->nextFree;
    }
    return maxSize;
}

void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t size) {
    if (block->size >= size + sizeof(FreeBlock)) {
        std::size_t remainingSize = block->size - size;
        FreeBlock* newBlock = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(block) + size);
        
        newBlock->size = remainingSize;
        newBlock->isFree = true;
        newBlock->data = reinterpret_cast<char*>(newBlock) + sizeof(BlockHeader);
        
        newBlock->nextPhysBlock = block->nextPhysBlock;
        if (newBlock->nextPhysBlock) {
            newBlock->nextPhysBlock->prevPhysBlock = newBlock;
        }
        newBlock->prevPhysBlock = block;
        block->nextPhysBlock = newBlock;
        
        block->size = size;
        
        insertFreeBlock(newBlock);
    }
}

void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    BlockHeader* next = block->nextPhysBlock;
    if (next && next->isFree) {
        FreeBlock* nextFree = static_cast<FreeBlock*>(next);
        removeFreeBlock(nextFree);
        block->size += next->size;
        block->nextPhysBlock = next->nextPhysBlock;
        if (block->nextPhysBlock) {
            block->nextPhysBlock->prevPhysBlock = block;
        }
    }
    
    BlockHeader* prev = block->prevPhysBlock;
    if (prev && prev->isFree) {
        FreeBlock* prevFree = static_cast<FreeBlock*>(prev);
        removeFreeBlock(prevFree);
        prev->size += block->size;
        prev->nextPhysBlock = block->nextPhysBlock;
        if (prev->nextPhysBlock) {
            prev->nextPhysBlock->prevPhysBlock = prev;
        }
        block = prevFree;
    }
    
    insertFreeBlock(block);
}

TLSFAllocator::FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli;
    mappingFunction(size, fli, sli);
    
    std::uint32_t sliMap = index.sliBitmaps[fli] & (~0U << sli);
    if (sliMap != 0) {
        int found_sli = ffs(sliMap);
        return index.freeLists[fli][found_sli];
    }
    
    std::uint32_t fliMap = index.fliBitmap & (~0U << (fli + 1));
    if (fliMap != 0) {
        int found_fli = ffs(fliMap);
        int found_sli = ffs(index.sliBitmaps[found_fli]);
        return index.freeLists[found_fli][found_sli];
    }
    
    return nullptr;
}

void TLSFAllocator::insertFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    block->nextFree = index.freeLists[fli][sli];
    block->prevFree = nullptr;
    if (index.freeLists[fli][sli]) {
        index.freeLists[fli][sli]->prevFree = block;
    }
    index.freeLists[fli][sli] = block;
    
    index.fliBitmap |= (1U << fli);
    index.sliBitmaps[fli] |= (1U << sli);
}

void TLSFAllocator::removeFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        index.freeLists[fli][sli] = block->nextFree;
    }
    
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    
    if (index.freeLists[fli][sli] == nullptr) {
        index.sliBitmaps[fli] &= ~(1U << sli);
        if (index.sliBitmaps[fli] == 0) {
            index.fliBitmap &= ~(1U << fli);
        }
    }
}

void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    fli = fls(size);
    if (fli < SLI_BITS) {
        sli = size - (1ULL << fli); // This is only correct if size is small
        // But wait, if fli < SLI_BITS, then 2^fli < 16.
        // The number of divisions is 2^fli.
        // The bin width is 1.
        // So sli = size - 2^fli is correct and will be < 16.
    } else {
        sli = (size - (1ULL << fli)) >> (fli - SLI_BITS);
    }
}
