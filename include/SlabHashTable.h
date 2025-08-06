#ifndef SLAB_HASH_TABLE_H
#define SLAB_HASH_TABLE_H

#include "NvmDefs.h"       // For uint64_t, NVM_CHUNK_SIZE
#include "NvmSlab.h"       // For NvmSlab* type

/**
 * @struct SlabHashNode
 * @brief 哈希表中的节点，用于存储一个键值对。
 *
 * 这些节点在发生哈希冲突时，会形成一个单向链表（拉链）。
 */
typedef struct SlabHashNode {
    uint64_t             nvm_offset;   // Key: Slab 的基地址偏移量
    NvmSlab*             slab_ptr;     // Value: 指向 Slab 元数据结构体的指针
    struct SlabHashNode* next;         // 指向同一个哈希桶中下一个节点的指针
} SlabHashNode;

/**
 * @struct SlabHashTable
 * @brief 一个专门用于映射 "NVM偏移量 -> Slab指针" 的哈希表。
 */
typedef struct SlabHashTable {
    SlabHashNode** buckets;      // 指向哈希桶数组的指针 (数组的每个元素是一个 SlabHashNode* 指针)
    uint32_t       capacity;     // 哈希桶的数量（数组的大小）
    uint32_t       count;        // 哈希表中存储的元素总数
} SlabHashTable;

// ============================================================================
//                          公共 API 函数
// ============================================================================

/**
 * @brief 创建并初始化一个新的 Slab 哈希表。
 *
 * @param initial_capacity 哈希表的初始容量（桶的数量）。
 *                         推荐使用一个素数，或者2的幂，以获得更好的哈希分布。
 * @return 成功则返回指向新哈希表的指针，失败则返回NULL。
 */
SlabHashTable* slab_hashtable_create(uint32_t initial_capacity);

/**
 * @brief 销毁一个 Slab 哈希表，并释放所有其管理的元数据。
 *
 * @note 此函数只释放哈希表本身和其内部的哈希节点（SlabHashNode），
 *       并不会释放节点中存储的 Slab 指针（slab_ptr）。
 *       Slab 对象的生命周期由更高层的模块管理。
 *
 * @param table 指向由 slab_hashtable_create 创建的哈希表对象的指针。
 */
void slab_hashtable_destroy(SlabHashTable* table);

/**
 * @brief 向哈希表中插入一个新的 Slab 映射。
 *
 * 如果具有相同 nvm_offset 的键已存在，此操作将失败。
 *
 * @param table 指向哈希表的指针。
 * @param nvm_offset Slab 的基地址偏移量 (Key)。
 * @param slab_ptr 指向要插入的 Slab 对象的指针 (Value)。
 * @return 0表示成功, -1表示失败 (如内存不足或键已存在)。
 */
int slab_hashtable_insert(SlabHashTable* table, uint64_t nvm_offset, NvmSlab* slab_ptr);

/**
 * @brief 从哈希表中查找一个 Slab。
 *
 * @param table 指向哈希表的指针。
 * @param nvm_offset 要查找的 Slab 的基地址偏移量。
 * @return 如果找到，返回指向 Slab 对象的指针；如果未找到，返回NULL。
 */
NvmSlab* slab_hashtable_lookup(SlabHashTable* table, uint64_t nvm_offset);

/**
 * @brief 从哈希表中移除一个 Slab 映射。
 *
 * @param table 指向哈希表的指针。
 * @param nvm_offset 要移除的 Slab 的基地址偏移量。
 * @return 如果找到并成功移除，返回指向被移除的 Slab 对象的指针；如果未找到，返回NULL。
 */
NvmSlab* slab_hashtable_remove(SlabHashTable* table, uint64_t nvm_offset);

#endif // SLAB_HASH_TABLE_H