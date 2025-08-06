#ifndef NVM_ALLOCATOR_H
#define NVM_ALLOCATOR_H

#include "NvmSpaceManager.h"
#include "SlabHashTable.h"
#include "NvmSlab.h"
#include "NvmDefs.h"

/**
 * @struct NvmAllocator
 * @brief 顶层NVM堆分配器，整合了所有底层组件。
 */
typedef struct {
    // 1. 底层大块内存管理器 (土地管理局)
    FreeSpaceManager* space_manager;

    // 2. 用于从地址快速查找Slab的哈希表 (市政地址簿)
    SlabHashTable* slab_lookup_table;
    
    // 3. 按尺寸类别组织的Slab链表 (不同户型的公寓楼列表)
    //    这是一个数组，每个元素是一个指向特定尺寸Slab链表头部的指针。
    NvmSlab* slab_lists[SC_COUNT];

} NvmAllocator;

/**
 * @brief 创建并初始化一个NVM分配器。
 * @param total_nvm_size 要管理的总NVM空间大小。
 * @param nvm_start_offset NVM空间的起始偏移量。
 * @return 成功则返回指向新分配器的指针，失败则返回NULL。
 */
NvmAllocator* nvm_allocator_create(uint64_t total_nvm_size, uint64_t nvm_start_offset);

/**
 * @brief 销毁一个NVM分配器，并释放其管理的所有资源。
 * @param allocator 指向由 nvm_allocator_create 创建的分配器。
 */
void nvm_allocator_destroy(NvmAllocator* allocator);

/**
 * @brief 从NVM中分配一块指定大小的内存。
 * @param allocator 指向NVM分配器的指针。
 * @param size 请求的内存大小（字节）。
 * @return 成功则返回分配到的NVM内存的偏移量，失败则返回 (uint64_t)-1。
 */
uint64_t nvm_malloc(NvmAllocator* allocator, size_t size);

/**
 * @brief 释放一块之前分配的NVM内存。
 * @param allocator 指向NVM分配器的指针。
 * @param nvm_offset 要释放的内存的偏移量。
 */
void nvm_free(NvmAllocator* allocator, uint64_t nvm_offset);

#endif // NVM_ALLOCATOR_H