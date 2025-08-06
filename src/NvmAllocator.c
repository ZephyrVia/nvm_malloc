#include "NvmAllocator.h"
#include <stdlib.h> // for malloc, free
#include <string.h> // for NULL
#include <assert.h>

// 初始哈希表的容量，选择一个素数
#define INITIAL_HASHTABLE_CAPACITY 101


/**
 * @brief (内部) 将请求的大小映射到最合适的尺寸类别ID。
 *
 * 采用向上取整的策略，为请求找到最小的、能容纳它的块。
 *
 * @param size 请求的内存大小。
 * @return 对应的SizeClassID，如果请求过大则返回SC_COUNT。
 */
static SizeClassID map_size_to_sc_id(size_t size) {
    if (size <= 8)    return SC_8B;
    if (size <= 16)   return SC_16B;
    if (size <= 32)   return SC_32B;
    if (size <= 64)   return SC_64B;
    if (size <= 128)  return SC_128B;
    if (size <= 256)  return SC_256B;
    if (size <= 512)  return SC_512B;
    if (size <= 1024) return SC_1K;
    if (size <= 2048) return SC_2K;
    if (size <= 4096) return SC_4K;
    return SC_COUNT; // 请求的大小超过了最大Slab块，无法处理
}


NvmAllocator* nvm_allocator_create(uint64_t total_nvm_size, uint64_t nvm_start_offset) {
    // 1. 为 NvmAllocator 结构体本身分配内存
    NvmAllocator* allocator = (NvmAllocator*)malloc(sizeof(NvmAllocator));
    if (allocator == NULL) {
        return NULL;
    }

    // 2. 创建底层组件
    allocator->space_manager = space_manager_create(total_nvm_size, nvm_start_offset);
    allocator->slab_lookup_table = slab_hashtable_create(INITIAL_HASHTABLE_CAPACITY);

    // 检查组件是否创建成功
    if (allocator->space_manager == NULL || allocator->slab_lookup_table == NULL) {
        // 如果任何一个组件创建失败，则销毁已创建的组件并释放自身
        space_manager_destroy(allocator->space_manager);
        slab_hashtable_destroy(allocator->slab_lookup_table);
        free(allocator);
        return NULL;
    }

    // 3. 初始化 slab_lists 数组
    for (int i = 0; i < SC_COUNT; ++i) {
        allocator->slab_lists[i] = NULL;
    }

    return allocator;
}


void nvm_allocator_destroy(NvmAllocator* allocator) {
    if (allocator == NULL) {
        return;
    }

    // 1. 销毁所有Slab对象
    //    哈希表只存指针，真正的Slab对象在slab_lists链表中
    for (int i = 0; i < SC_COUNT; ++i) {
        NvmSlab* current_slab = allocator->slab_lists[i];
        while (current_slab != NULL) {
            NvmSlab* slab_to_destroy = current_slab;
            current_slab = current_slab->next_in_chain;
            nvm_slab_destroy(slab_to_destroy);
        }
    }

    // 2. 销毁底层组件
    space_manager_destroy(allocator->space_manager);
    slab_hashtable_destroy(allocator->slab_lookup_table);

    // 3. 释放 NvmAllocator 结构体本身
    free(allocator);
}

uint64_t nvm_malloc(NvmAllocator* allocator, size_t size) {
    if (allocator == NULL || size == 0) {
        return (uint64_t)-1;
    }

    // 1. 映射大小到尺寸类别
    SizeClassID sc_id = map_size_to_sc_id(size);
    if (sc_id == SC_COUNT) {
        // 请求过大，当前Slab系统无法处理
        // TODO: 未来可以增加对大对象的直接分配逻辑
        return (uint64_t)-1;
    }

    // 2. 查找一个可用的Slab
    NvmSlab* target_slab = allocator->slab_lists[sc_id];
    while (target_slab != NULL && nvm_slab_is_full(target_slab)) {
        target_slab = target_slab->next_in_chain;
    }

    // 3. 如果没有找到，则创建一个新的Slab
    if (target_slab == NULL) {
        uint64_t new_slab_offset = space_manager_alloc_slab(allocator->space_manager);
        if (new_slab_offset == (uint64_t)-1) {
            return (uint64_t)-1; // 底层NVM空间耗尽
        }

        target_slab = nvm_slab_create(sc_id, new_slab_offset);
        if (target_slab == NULL) {
            // DRAM内存不足，无法创建Slab元数据
            // 必须归还刚刚申请的NVM chunk
            space_manager_free_slab(allocator->space_manager, new_slab_offset);
            return (uint64_t)-1;
        }

        // 注册新Slab
        slab_hashtable_insert(allocator->slab_lookup_table, new_slab_offset, target_slab);
        target_slab->next_in_chain = allocator->slab_lists[sc_id];
        allocator->slab_lists[sc_id] = target_slab;
    }

    // 4. 从目标Slab中分配块
    uint32_t block_idx;
    if (nvm_slab_alloc(target_slab, &block_idx) == 0) {
        // 计算最终偏移量
        return target_slab->nvm_base_offset + (block_idx * target_slab->block_size);
    }

    return (uint64_t)-1; // 理论上不应发生，但作为防御
}


static void remove_slab_from_list(NvmSlab** list_head, NvmSlab* slab_to_remove) {
    if (list_head == NULL || *list_head == NULL || slab_to_remove == NULL) {
        return;
    }

    // 情况一：要移除的是头节点
    if (*list_head == slab_to_remove) {
        *list_head = slab_to_remove->next_in_chain;
        return;
    }

    // 情况二：要移除的是中间或尾部节点
    NvmSlab* current = *list_head;
    while (current->next_in_chain != NULL && current->next_in_chain != slab_to_remove) {
        current = current->next_in_chain;
    }

    // 如果找到了
    if (current->next_in_chain == slab_to_remove) {
        current->next_in_chain = slab_to_remove->next_in_chain;
    }
}


void nvm_free(NvmAllocator* allocator, uint64_t nvm_offset) {
    if (allocator == NULL || nvm_offset == (uint64_t)-1 ) {
        return;
    }

    // 1. 计算所属Slab的基地址
    uint64_t slab_base_offset = (nvm_offset / NVM_SLAB_SIZE) * NVM_SLAB_SIZE;

    // 2. 在哈希表中快速查找Slab
    NvmSlab* target_slab = slab_hashtable_lookup(allocator->slab_lookup_table, slab_base_offset);

    if (target_slab == NULL) {
        // 错误：试图释放一个不被我们管理的地址
        assert(!"Attempting to free an unmanaged memory offset!");
        return;
    }

    // 3. 计算块在Slab内的索引
    uint32_t block_idx = (nvm_offset - target_slab->nvm_base_offset) / target_slab->block_size;

    // 4. 调用Slab的free方法
    nvm_slab_free(target_slab, block_idx);

    // 5. [核心回收逻辑] 检查Slab是否已经完全变空
    if (nvm_slab_is_empty(target_slab)) {
        // 为了防止一个Slab链表中只剩一个空Slab时被回收（我们倾向于保留每种尺寸至少一个Slab以备后用），
        // 我们可以加一个条件：只有当该尺寸的Slab数量大于1时，才回收空的。
        SizeClassID sc_id = target_slab->size_type_id;
        if (allocator->slab_lists[sc_id] != target_slab || target_slab->next_in_chain != NULL) {
            
            // a. 从 allocator->slab_lists 链表中移除此Slab
            remove_slab_from_list(&allocator->slab_lists[sc_id], target_slab);

            // b. 从哈希表中移除此Slab的条目
            slab_hashtable_remove(allocator->slab_lookup_table, target_slab->nvm_base_offset);

            // c. 将其占用的2MB NVM chunk归还给底层空间管理器
            space_manager_free_slab(allocator->space_manager, target_slab->nvm_base_offset);

            // d. 最后，销毁Slab的元数据对象本身
            nvm_slab_destroy(target_slab);
        }
    }
}