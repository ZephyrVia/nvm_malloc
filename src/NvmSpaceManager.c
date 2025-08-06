#include <stdlib.h>            // for malloc, free
#include <assert.h>            // for assert (可选，但推荐用于开发)
#include <stdbool.h>

#include "NvmDefs.h"
#include "NvmSpaceManager.h" // 假设头文件名为 nvm_space_manager.h


static FreeSegmentNode* create_segment_node(uint64_t offset, uint64_t size);

static void remove_node_from_list(FreeSpaceManager* manager, FreeSegmentNode* node);

static void insert_node_into_list(FreeSpaceManager* manager, FreeSegmentNode* new_node, FreeSegmentNode* prev_node, FreeSegmentNode* next_node);


/**
 * @brief 创建并初始化一个新的空闲空间管理器。
 *
 * 这是推荐的构造函数。它在堆上分配管理器并返回其指针。
 *
 * @param total_nvm_size 总的NVM空间大小。
 * @param nvm_start_offset NVM空间的起始偏移量。
 * @return 成功则返回指向新管理器的指针，失败则返回NULL。
 */
FreeSpaceManager* space_manager_create(uint64_t total_nvm_size, uint64_t nvm_start_offset) {
    // 1. 为管理器结构本身分配内存
    FreeSpaceManager* manager = (FreeSpaceManager*)malloc(sizeof(FreeSpaceManager));
    if (manager == NULL) {
        return NULL; // 内存不足
    }

    // 2. 初始化 head 和 tail
    manager->head = NULL;
    manager->tail = NULL;

    // 3. 验证参数
    if (total_nvm_size < NVM_SLAB_SIZE) {
        // 如果参数无效，需要释放刚刚分配的 manager
        free(manager);
        return NULL;
    }

    // 4. 创建初始节点
    FreeSegmentNode* initial_node = create_segment_node(nvm_start_offset, total_nvm_size);
    if (initial_node == NULL) {
        // 如果节点创建失败，也需要释放 manager
        free(manager);
        return NULL;
    }

    // 5. 设置 head 和 tail
    manager->head = initial_node;
    manager->tail = initial_node;

    return manager;
}

/**
 * @brief 销毁一个空闲空间管理器，并释放其本身。
 *
 * @param manager 指向由 space_manager_create 创建的管理器对象的指针。
 *                可以安全地传递一个 NULL 指针。
 */
void space_manager_destroy(FreeSpaceManager* manager) {
    if (manager == NULL) {
        return;
    }

    // 释放所有内部节点
    FreeSegmentNode* current = manager->head;
    while (current != NULL) {
        FreeSegmentNode* node_to_free = current;
        current = current->next;
        free(node_to_free);
    }
    
    free(manager);
}



uint64_t space_manager_alloc_slab(FreeSpaceManager* manager) {
    // 防御性检查
    if (manager == NULL) {
        return (uint64_t)-1;
    }

    // 1. 从 manager->head 开始遍历链表，寻找第一个足够大的节点。
    FreeSegmentNode* current = manager->head;
    while (current != NULL) {
        if (current->size >= NVM_SLAB_SIZE) {
            // 3. 找到了一个满足条件的节点 (found_node)
            FreeSegmentNode* found_node = current;
            
            // a. 记录要返回的地址
            uint64_t allocated_offset = found_node->nvm_offset;

            // b. 检查 found_node 的大小
            if (found_node->size == NVM_SLAB_SIZE) {
                // 情况一：大小正好，整个节点被用掉。
                // 我们需要将它从链表中移除并释放。
                remove_node_from_list(manager, found_node);
                free(found_node);
            } else {
                // 情况二：大小大于所需，分裂节点。
                // 只需更新节点的起始地址和大小，节点本身仍在链表中。
                found_node->nvm_offset += NVM_SLAB_SIZE;
                found_node->size -= NVM_SLAB_SIZE;
            }

            // c. 返回成功分配的块的偏移量
            return allocated_offset;
        }
        
        // 移动到下一个节点
        current = current->next;
    }

    // 2. 如果遍历完整个链表都没有找到，说明空间不足。
    return (uint64_t)-1;
}


void space_manager_free_slab(FreeSpaceManager* manager, uint64_t offset_to_free) {
    if (manager == NULL) {
        return;
    }

    // 1. 遍历链表，找到正确的插入/合并位置。
    // 我们需要找到 prev_node 和 next_node，使得:
    // prev_node->nvm_offset < offset_to_free < next_node->nvm_offset
    FreeSegmentNode* prev_node = NULL;
    FreeSegmentNode* next_node = manager->head;

    while (next_node != NULL && next_node->nvm_offset < offset_to_free) {
        prev_node = next_node;
        next_node = next_node->next;
    }
    
    // [可选] 防御性编程/断言：检查双重释放或重叠释放
    // 如果 next_node 存在，它的起始地址必须在被释放块的末尾之后。
    assert(next_node == NULL || (offset_to_free + NVM_SLAB_SIZE <= next_node->nvm_offset));
    // 如果 prev_node 存在，被释放块的起始地址必须在它的末尾之后。
    assert(prev_node == NULL || (prev_node->nvm_offset + prev_node->size <= offset_to_free));


    // 2. 检查是否可以向前合并
    bool can_merge_prev = (prev_node != NULL && (prev_node->nvm_offset + prev_node->size == offset_to_free));

    // 3. 检查是否可以向后合并
    bool can_merge_next = (next_node != NULL && (offset_to_free + NVM_SLAB_SIZE == next_node->nvm_offset));

    // 4. 根据合并情况执行操作
    if (can_merge_prev && can_merge_next) {
        // a. 三向合并 (前 + 当前 + 后)
        // 将 prev_node 的大小扩大，覆盖当前释放的块和 next_node
        prev_node->size += NVM_SLAB_SIZE + next_node->size;

        // 从链表中移除并释放 next_node
        remove_node_from_list(manager, next_node);
        free(next_node);

    } else if (can_merge_prev) {
        // b. 只向前合并
        // 只需扩大 prev_node 的大小即可
        prev_node->size += NVM_SLAB_SIZE;

    } else if (can_merge_next) {
        // c. 只向后合并
        // 更新 next_node 的起始地址和大小，使其包含当前释放的块
        next_node->nvm_offset = offset_to_free;
        next_node->size += NVM_SLAB_SIZE;

    } else {
        // d. 都不能合并，创建一个新的独立节点
        FreeSegmentNode* new_node = create_segment_node(offset_to_free, NVM_SLAB_SIZE);
        if (new_node == NULL) {
            // 内存不足，无法创建新节点。这是一个严重问题。
            // 在生产环境中，这里应该有错误日志记录。
            return;
        }
        // 将新节点插入到 prev_node 和 next_node 之间
        insert_node_into_list(manager, new_node, prev_node, next_node);
    }
}


/**
 * @brief (内部函数) 创建并初始化一个新的 FreeSegmentNode。
 */
static FreeSegmentNode* create_segment_node(uint64_t offset, uint64_t size) {
    // 1. 使用 malloc 分配内存
    FreeSegmentNode* node = (FreeSegmentNode*)malloc(sizeof(FreeSegmentNode));

    // 2. 检查 malloc 是否成功
    if (node == NULL) {
        // 在实际项目中，这里可能需要记录日志
        return NULL;
    }

    // 3. 初始化节点的成员变量
    node->nvm_offset = offset;
    node->size = size;
    node->prev = NULL;
    node->next = NULL;

    // 4. 返回新创建的节点指针
    return node;
}


/**
 * @brief (内部函数) 将一个节点从双向链表中安全地解链。
 */
static void remove_node_from_list(FreeSpaceManager* manager, FreeSegmentNode* node) {
    assert(manager != NULL && node != NULL);

    // 获取前一个和后一个节点
    FreeSegmentNode* prev_node = node->prev;
    FreeSegmentNode* next_node = node->next;

    // 更新前一个节点的 'next' 指针
    if (prev_node != NULL) {
        prev_node->next = next_node;
    } else {
        // 如果没有前一个节点，说明被移除的是头节点
        // 更新管理器的 head 指针
        manager->head = next_node;
    }

    // 更新后一个节点的 'prev' 指针
    if (next_node != NULL) {
        next_node->prev = prev_node;
    } else {
        // 如果没有后一个节点，说明被移除的是尾节点
        // 更新管理器的 tail 指针
        manager->tail = prev_node;
    }

    // 将被移除节点的指针清空，这是一个好习惯，可以防止悬垂指针问题
    node->prev = NULL;
    node->next = NULL;
}


/**
 * @brief (内部函数) 将一个新节点插入到双向链表中的正确位置。
 */
static void insert_node_into_list(FreeSpaceManager* manager, FreeSegmentNode* new_node, FreeSegmentNode* prev_node, FreeSegmentNode* next_node) {
    assert(manager != NULL && new_node != NULL);

    // 1. 设置新节点的 prev 和 next 指针
    new_node->prev = prev_node;
    new_node->next = next_node;

    // 2. 更新 prev_node 的 next 指针
    if (prev_node != NULL) {
        prev_node->next = new_node;
    } else {
        // 如果 prev_node 为 NULL, 说明新节点是新的头节点
        manager->head = new_node;
    }

    // 3. 更新 next_node 的 prev 指针
    if (next_node != NULL) {
        next_node->prev = new_node;
    } else {
        // 如果 next_node 为 NULL, 说明新节点是新的尾节点
        manager->tail = new_node;
    }
}