#ifndef NVM_SPACE_MANAGER_H
#define NVM_SPACE_MANAGER_H

#include <stdint.h> // For uint64_t
#include <stddef.h> // For NULL

/**
 * @struct FreeSegmentNode
 * @brief 表示一个连续的、空闲的NVM空间块的节点。
 *
 * 这些节点构成一个双向链表，整个链表按照 nvm_offset 从小到大排序。
 * 这个排序是实现高效合并的关键。
 */
typedef struct FreeSegmentNode {
    // 这个空闲块在NVM中的起始地址（偏移量）
    uint64_t nvm_offset;

    // 这个空闲块的大小，必须是 NVM_CHUNK_SIZE 的整数倍。
    uint64_t size;

    // 指向链表中前一个节点的指针（地址更小的空闲块）
    struct FreeSegmentNode* prev;

    // 指向链表中后一个节点的指针（地址更大的空闲块）
    struct FreeSegmentNode* next;

} FreeSegmentNode;


/**
 * @struct FreeSpaceManager
 * @brief NVM空闲空间链表的管理器。
 *
 * 这个结构体是整个空闲空间管理机制的入口点。它持有链表的头/尾指针
 * 以及一些用于快速查询和统计的元数据。
 */
typedef struct FreeSpaceManager {
    // 指向链表的头部（NVM地址最小的空闲块）
    FreeSegmentNode* head;

    // 指向链表的尾部（NVM地址最大的空闲块）
    FreeSegmentNode* tail;

} FreeSpaceManager;


FreeSpaceManager* space_manager_create(uint64_t total_nvm_size, uint64_t nvm_start_offset);
void space_manager_destroy(FreeSpaceManager* manager);

// 其他API
uint64_t space_manager_alloc_slab(FreeSpaceManager* manager);
void space_manager_free_slab(FreeSpaceManager* manager, uint64_t offset_to_free);

#endif // NVM_SPACE_MANAGER_H