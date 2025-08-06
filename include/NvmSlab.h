#ifndef NVM_SLAB_H
#define NVM_SLAB_H

#include <stdbool.h>

#include "NvmDefs.h"

#define SLAB_CACHE_SIZE (64)
#define SLAB_CACHE_BATCH_SIZE (SLAB_CACHE_SIZE / 2)

typedef struct NvmSlab {

    // 指向下一个同类Slab的指针，用于构建大小类的活跃链表。
    struct NvmSlab* next_in_chain;

    // 本Slab在NVM上的全局唯一ID（起始偏移量）。用于被全局的“地址->Slab”索引快速定位。
    uint64_t nvm_base_offset;

    // 本Slab所属的大小类型ID (e.g., SC_256B).
    uint8_t size_type_id;
    
    // (为对齐填充，编译器通常会自动处理)
    uint8_t _padding[3];

    // 本Slab分配的块的固定大小 (e.g., 256 bytes).
    uint32_t block_size;

    // 本Slab总共可以容纳的块的数量 (e.g., 8192).
    uint32_t total_block_count;

    // 当前已分配的块的总数量，是动态变化的。该字段对缓存操作没有感知，只负责追踪最外层的块分配
    uint32_t allocated_block_count;

    // FIFO环形缓冲区 - 头部索引.指向缓存数组中下一个将被分配出去的元素的索引。
    uint32_t cache_head;

    // FIFO环形缓冲区 - 尾部索引.指向缓存数组中下一个可用于存放被释放元素的空位的索引。
    uint32_t cache_tail;

    // FIFO环形缓冲区 - 当前元素数量.精确地记录了缓存中当前有多少个空闲块索引。用于判断缓存是空、是满，以及与高低水位线进行比较。
    uint32_t cache_count;

    // FIFO环形缓冲区 - 存储空闲块索引的数组.这是实现先进先出缓存的物理存储。
    uint32_t free_block_buffer[SLAB_CACHE_SIZE];

    // 权威记录位图 (柔性数组成员，必须在最后)。
    unsigned char bitmap[];

} NvmSlab;



NvmSlab* nvm_slab_create(SizeClassID sc_id, uint64_t nvm_base_offset);

void nvm_slab_destroy(NvmSlab* self);

int nvm_slab_alloc(NvmSlab* self, uint32_t* out_block_idx);

void nvm_slab_free(NvmSlab* self, uint32_t block_idx);

bool nvm_slab_is_full(const NvmSlab* self);

bool nvm_slab_is_empty(const NvmSlab* self);

#endif // NVM_SLAB_H