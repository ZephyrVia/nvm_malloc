#ifndef NVM_ALLOCATOR_H
#define NVM_ALLOCATOR_H

#include "NvmSpaceManager.h"
#include "SlabHashTable.h"
#include "NvmSlab.h"
#include "NvmDefs.h"

#define DEFAULT_NVM_START_OFFSET  0

// NVM堆分配器核心结构
typedef struct {
    void*             nvm_base_addr;    // NVM在内存中的映射基地址
    FreeSpaceManager* space_manager;    // 大块空闲空间管理器
    SlabHashTable* slab_lookup_table;   // 用于通过偏移量快速查找Slab
    NvmSlab* slab_lists[SC_COUNT];      // 按尺寸分类的Slab链表数组
} NvmAllocator;


// 创建并初始化NVM分配器
NvmAllocator* nvm_allocator_create(void* nvm_base_addr, uint64_t nvm_size_bytes);

// 销毁NVM分配器并释放其资源
void nvm_allocator_destroy(NvmAllocator* allocator);

// 从NVM分配一块内存
void* nvm_malloc(NvmAllocator* allocator, size_t size);

// 释放指定的NVM内存
void nvm_free(NvmAllocator* allocator, void* nvm_ptr);

// 恢复一个之前已分配的内存块
int nvm_allocator_restore_allocation(NvmAllocator* allocator, void* nvm_ptr, size_t size);

#endif // NVM_ALLOCATOR_H