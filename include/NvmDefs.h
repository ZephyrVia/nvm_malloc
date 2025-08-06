#ifndef NVM_DEFS_H_ 
#define NVM_DEFS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#define NVM_SLAB_SIZE (2 * 1024 * 1024)

/**
 * @brief 定义了不同类型的 Slab，主要根据其管理的块大小来划分。
 *        这些ID将作为上层管理器中大小类数组的索引。
 */
typedef enum {
    SC_8B,       // 8字节
    SC_16B,      // 16字节
    SC_32B,      // 32字节
    SC_64B,      // 64字节
    SC_128B,     // 128字节
    SC_256B,     // 256字节
    SC_512B,     // 512字节
    SC_1K,       // 1024字节
    SC_2K,       // 2048字节
    SC_4K,       // 4096字节
    SC_COUNT     // 特殊成员，自动表示大小类的总数
} SizeClassID;


#endif // NVM_DEFS_H_ 