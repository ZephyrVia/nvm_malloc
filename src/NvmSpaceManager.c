#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "NvmDefs.h"
#include "NvmSpaceManager.h"

// ============================================================================
//                          核心数据结构
// ============================================================================

// 空闲段节点：按 NVM 偏移地址排序的双向链表
typedef struct FreeSegmentNode {
    uint64_t nvm_offset;
    uint64_t size;
    struct FreeSegmentNode* prev;
    struct FreeSegmentNode* next;
} FreeSegmentNode;

// 空间管理器
typedef struct FreeSpaceManager {
    FreeSegmentNode* head;
    FreeSegmentNode* tail;
    nvm_mutex_t      lock;
} FreeSpaceManager;

// ============================================================================
//                          内部函数前向声明
// ============================================================================

static FreeSegmentNode* create_segment_node(uint64_t offset, uint64_t size);
static void remove_node_from_list(FreeSpaceManager* manager, FreeSegmentNode* node);
static void insert_node_into_list(FreeSpaceManager* manager, FreeSegmentNode* new_node, 
                                  FreeSegmentNode* prev_node, FreeSegmentNode* next_node);

// ============================================================================
//                          公共 API 实现
// ============================================================================

FreeSpaceManager* space_manager_create(uint64_t total_nvm_size, uint64_t nvm_start_offset) {
    if (total_nvm_size < NVM_SLAB_SIZE) {
        LOG_ERR("Total size (%llu) smaller than slab size (%llu).", 
                (unsigned long long)total_nvm_size, (unsigned long long)NVM_SLAB_SIZE);
        return NULL;
    }

    FreeSpaceManager* manager = (FreeSpaceManager*)malloc(sizeof(FreeSpaceManager));
    if (!manager) {
        LOG_ERR("Failed to allocate manager struct.");
        return NULL;
    }

    manager->head = NULL;
    manager->tail = NULL;

    if (NVM_MUTEX_INIT(&manager->lock) != 0) {
        LOG_ERR("Failed to init mutex.");
        goto err_free_manager;
    }

    // 创建初始的大块空闲节点
    FreeSegmentNode* initial_node = create_segment_node(nvm_start_offset, total_nvm_size);
    if (!initial_node) {
        LOG_ERR("Failed to create initial segment.");
        goto err_destroy_mutex;
    }

    manager->head = initial_node;
    manager->tail = initial_node;

    return manager;

err_destroy_mutex:
    NVM_MUTEX_DESTROY(&manager->lock);
err_free_manager:
    free(manager);
    return NULL;
}

void space_manager_destroy(FreeSpaceManager* manager) {
    if (!manager) return;

    FreeSegmentNode* current = manager->head;
    while (current) {
        FreeSegmentNode* next = current->next;
        free(current);
        current = next;
    }
    
    NVM_MUTEX_DESTROY(&manager->lock);
    free(manager);
}

uint64_t space_manager_alloc_slab(FreeSpaceManager* manager) {
    if (!manager) return (uint64_t)-1;

    NVM_MUTEX_ACQUIRE(&manager->lock);

    // [First-Fit] 查找首个满足大小的节点
    FreeSegmentNode* curr = manager->head;
    while (curr) {
        if (curr->size >= NVM_SLAB_SIZE) {
            uint64_t offset = curr->nvm_offset;

            if (curr->size == NVM_SLAB_SIZE) {
                // 大小刚好，移除节点
                remove_node_from_list(manager, curr);
                free(curr);
            } else {
                // 空间富余，切割节点
                curr->nvm_offset += NVM_SLAB_SIZE;
                curr->size       -= NVM_SLAB_SIZE;
            }

            NVM_MUTEX_RELEASE(&manager->lock);
            return offset;
        }
        curr = curr->next;
    }

    NVM_MUTEX_RELEASE(&manager->lock);
    return (uint64_t)-1;
}

void space_manager_free_slab(FreeSpaceManager* manager, uint64_t offset_to_free) {
    if (!manager) return;

    NVM_MUTEX_ACQUIRE(&manager->lock);

    // 查找插入位置 (prev < offset < next)
    FreeSegmentNode* prev = NULL;
    FreeSegmentNode* next = manager->head;
    while (next && next->nvm_offset < offset_to_free) {
        prev = next;
        next = next->next;
    }

    // 校验重叠 (Debug 模式)
    assert(!next || (offset_to_free + NVM_SLAB_SIZE <= next->nvm_offset));
    assert(!prev || (prev->nvm_offset + prev->size <= offset_to_free));

    bool merge_prev = (prev && (prev->nvm_offset + prev->size == offset_to_free));
    bool merge_next = (next && (offset_to_free + NVM_SLAB_SIZE == next->nvm_offset));

    if (merge_prev && merge_next) {
        // 双向合并：Prev + Self + Next
        prev->size += NVM_SLAB_SIZE + next->size;
        remove_node_from_list(manager, next);
        free(next);
    } else if (merge_prev) {
        // 向前合并
        prev->size += NVM_SLAB_SIZE;
    } else if (merge_next) {
        // 向后合并 (节点前移)
        next->nvm_offset = offset_to_free;
        next->size      += NVM_SLAB_SIZE;
    } else {
        // 无法合并，插入新节点
        FreeSegmentNode* node = create_segment_node(offset_to_free, NVM_SLAB_SIZE);
        if (!node) {
            LOG_ERR("Failed to create free segment node (Memory Leak!).");
            // 无法插入回链表，只能丢弃该块（这属于严重系统错误）
        } else {
            insert_node_into_list(manager, node, prev, next);
        }
    }

    NVM_MUTEX_RELEASE(&manager->lock);
}

int space_manager_alloc_at_offset(FreeSpaceManager* manager, uint64_t offset) {
    if (!manager) return -1;

    const uint64_t req_size = NVM_SLAB_SIZE;
    uint64_t req_end = offset + req_size;

    // 遍历查找包含目标区域的节点
    FreeSegmentNode* curr = manager->head;
    while (curr) {
        uint64_t curr_end = curr->nvm_offset + curr->size;

        if (curr->nvm_offset <= offset && curr_end >= req_end) {
            bool match_head = (curr->nvm_offset == offset);
            bool match_tail = (curr_end == req_end);

            if (match_head && match_tail) {
                // 情况 1: 完全重合 -> 移除节点
                remove_node_from_list(manager, curr);
                free(curr);
            } else if (match_head) {
                // 情况 2: 头部重合 -> 头部缩进
                curr->nvm_offset += req_size;
                curr->size       -= req_size;
            } else if (match_tail) {
                // 情况 3: 尾部重合 -> 尾部截断
                curr->size -= req_size;
            } else {
                // 情况 4: 中间挖空 -> 分裂节点
                FreeSegmentNode* new_tail = create_segment_node(req_end, curr_end - req_end);
                if (!new_tail) {
                    LOG_ERR("Failed to create split node during restore.");
                    return -1;
                }
                // 修改前段大小，插入后段节点
                curr->size = offset - curr->nvm_offset;
                insert_node_into_list(manager, new_tail, curr, curr->next);
            }
            return 0; // 成功
        }
        curr = curr->next;
    }

    LOG_ERR("Requested offset %llu is not free.", (unsigned long long)offset);
    return -1;
}

// ============================================================================
//                          内部函数实现
// ============================================================================

static FreeSegmentNode* create_segment_node(uint64_t offset, uint64_t size) {
    FreeSegmentNode* node = (FreeSegmentNode*)malloc(sizeof(FreeSegmentNode));
    if (node) {
        node->nvm_offset = offset;
        node->size       = size;
        node->prev       = NULL;
        node->next       = NULL;
    } else {
        LOG_ERR("Malloc failed for segment node.");
    }
    return node;
}

static void remove_node_from_list(FreeSpaceManager* manager, FreeSegmentNode* node) {
    if (node->prev) node->prev->next = node->next;
    else            manager->head    = node->next;

    if (node->next) node->next->prev = node->prev;
    else            manager->tail    = node->prev;

    node->prev = NULL;
    node->next = NULL;
}

static void insert_node_into_list(FreeSpaceManager* manager, FreeSegmentNode* new_node, 
                                  FreeSegmentNode* prev, FreeSegmentNode* next) {
    new_node->prev = prev;
    new_node->next = next;

    if (prev) prev->next     = new_node;
    else      manager->head  = new_node;

    if (next) next->prev     = new_node;
    else      manager->tail  = new_node;
}