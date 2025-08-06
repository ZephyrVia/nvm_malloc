#include "SlabHashTable.h"
#include "SlabHashTable.h"
#include <stdlib.h> // for malloc, free, NULL
#include <string.h> // for memset (可选，但推荐)


/**
 * @brief (内部) 哈希函数。将NVM偏移量转换为哈希桶的索引。
 * 
 * 利用了 key 总是 NVM_SLAB_SIZE 倍数的特性来优化。
 */
static uint32_t hash_function(const SlabHashTable* table, uint64_t key) {
    // 关键优化：不直接对地址哈希，而是对地址对应的 slab 索引进行哈希。
    uint64_t slab_index = key / NVM_SLAB_SIZE;
    return slab_index % table->capacity;
}

/**
 * @brief (内部) 创建一个新的哈希节点。
 */
static SlabHashNode* create_hash_node(uint64_t nvm_offset, NvmSlab* slab_ptr) {
    SlabHashNode* node = (SlabHashNode*)malloc(sizeof(SlabHashNode));
    if (node == NULL) {
        return NULL;
    }
    node->nvm_offset = nvm_offset;
    node->slab_ptr = slab_ptr;
    node->next = NULL;
    return node;
}


// ============================================================================
//                          公共 API 函数实现
// ============================================================================

// slab_hashtable_create 和 slab_hashtable_destroy 的实现...
SlabHashTable* slab_hashtable_create(uint32_t initial_capacity) {
    if (initial_capacity == 0) {
        return NULL;
    }
    SlabHashTable* table = (SlabHashTable*)malloc(sizeof(SlabHashTable));
    if (table == NULL) {
        return NULL;
    }
    table->buckets = (SlabHashNode**)calloc(initial_capacity, sizeof(SlabHashNode*));
    if (table->buckets == NULL) {
        free(table);
        return NULL;
    }
    table->capacity = initial_capacity;
    table->count = 0;
    return table;
}

void slab_hashtable_destroy(SlabHashTable* table) {
    if (table == NULL) {
        return;
    }
    for (uint32_t i = 0; i < table->capacity; ++i) {
        SlabHashNode* current_node = table->buckets[i];
        while (current_node != NULL) {
            SlabHashNode* node_to_free = current_node;
            current_node = current_node->next;
            free(node_to_free);
        }
    }
    free(table->buckets);
    free(table);
}


/**
 * @brief 向哈希表中插入一个新的 Slab 映射。
 */
int slab_hashtable_insert(SlabHashTable* table, uint64_t nvm_offset, NvmSlab* slab_ptr) {
    if (table == NULL || slab_ptr == NULL) {
        return -1; // 无效参数
    }
    
    // 1. 计算哈希值，确定要操作的桶
    uint32_t bucket_index = hash_function(table, nvm_offset);

    // 2. 检查键是否已存在。遍历桶中的链表。
    SlabHashNode* current_node = table->buckets[bucket_index];
    while (current_node != NULL) {
        if (current_node->nvm_offset == nvm_offset) {
            return -1; // 键已存在，插入失败
        }
        current_node = current_node->next;
    }

    // 3. 创建一个新的哈希节点
    SlabHashNode* new_node = create_hash_node(nvm_offset, slab_ptr);
    if (new_node == NULL) {
        return -1; // 内存不足
    }

    // 4. 将新节点插入到桶链表的头部（头插法最简单高效）
    new_node->next = table->buckets[bucket_index];
    table->buckets[bucket_index] = new_node;

    // 5. 更新哈希表中的元素总数
    table->count++;

    return 0; // 插入成功
}


/**
 * @brief 从哈希表中查找一个 Slab。
 */
NvmSlab* slab_hashtable_lookup(SlabHashTable* table, uint64_t nvm_offset) {
    if (table == NULL) {
        return NULL;
    }

    // 1. 计算哈希值，找到对应的桶
    uint32_t bucket_index = hash_function(table, nvm_offset);

    // 2. 遍历桶中的链表，查找匹配的键
    SlabHashNode* current_node = table->buckets[bucket_index];
    while (current_node != NULL) {
        if (current_node->nvm_offset == nvm_offset) {
            // 找到了！返回对应的 Slab 指针
            return current_node->slab_ptr;
        }
        current_node = current_node->next;
    }

    // 3. 遍历完链表仍未找到，返回 NULL
    return NULL;
}


/**
 * @brief 从哈希表中移除一个 Slab 映射。
 */
NvmSlab* slab_hashtable_remove(SlabHashTable* table, uint64_t nvm_offset) {
    if (table == NULL) {
        return NULL;
    }

    // 1. 计算哈希值，找到对应的桶
    uint32_t bucket_index = hash_function(table, nvm_offset);

    // 2. 遍历桶中的链表，查找要删除的节点
    SlabHashNode* current_node = table->buckets[bucket_index];
    SlabHashNode* prev_node = NULL;

    while (current_node != NULL) {
        if (current_node->nvm_offset == nvm_offset) {
            // 找到了要删除的节点

            // a. 将节点从链表中解链
            if (prev_node == NULL) {
                // 情况一：要删除的是头节点
                table->buckets[bucket_index] = current_node->next;
            } else {
                // 情况二：要删除的是中间或尾部节点
                prev_node->next = current_node->next;
            }

            // b. 准备返回值并释放节点
            NvmSlab* slab_to_return = current_node->slab_ptr;
            free(current_node);

            // c. 更新哈希表中的元素总数
            table->count--;

            // d. 返回被移除的 Slab 指针
            return slab_to_return;
        }
        
        // 移动到下一个节点
        prev_node = current_node;
        current_node = current_node->next;
    }

    // 3. 遍历完链表仍未找到，返回 NULL
    return NULL;
}