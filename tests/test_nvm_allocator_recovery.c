#include "unity.h"

// 包含所有必要的头文件
#include "NvmDefs.h"
#include "NvmSlab.h"
#include "NvmSpaceManager.h"
#include "SlabHashTable.h"
#include "NvmAllocator.h"

// 包含所有组件的实现文件，以进行白盒测试。
#include "NvmSlab.c"
#include "NvmSpaceManager.c"
#include "SlabHashTable.c"
#include "NvmAllocator.c"

#include <stdlib.h>
#include <string.h>

// 为集成测试定义一个模拟NVM空间
#define TOTAL_NVM_SIZE (10 * NVM_SLAB_SIZE) // 20MB
#define NUM_SLABS 10

// MODIFIED: 引入模拟NVM基地址的全局变量
static void* mock_nvm_base = NULL;
// 全局分配器，在每个测试开始时创建，结束时销毁
static NvmAllocator* g_allocator = NULL;

void setUp(void) {
    // MODIFIED: 为每个测试分配模拟NVM内存
    mock_nvm_base = malloc(TOTAL_NVM_SIZE);
    TEST_ASSERT_NOT_NULL(mock_nvm_base);
    memset(mock_nvm_base, 0, TOTAL_NVM_SIZE);

    // MODIFIED: 使用新的接口创建分配器
    g_allocator = nvm_allocator_create(mock_nvm_base, TOTAL_NVM_SIZE);
    TEST_ASSERT_NOT_NULL(g_allocator);
}

void tearDown(void) {
    nvm_allocator_destroy(g_allocator);
    g_allocator = NULL;

    // MODIFIED: 释放模拟NVM内存
    free(mock_nvm_base);
    mock_nvm_base = NULL;
}

// ============================================================================
//         测试 nvm_allocator_restore_allocation 函数
// ============================================================================

/**
 * @brief 测试基本路径：恢复单个对象，这将触发新Slab的创建。
 */
void test_restore_first_object_in_new_slab(void) {
    const uint64_t obj_offset = 2 * NVM_SLAB_SIZE + 64;
    const size_t obj_size = 60;
    const uint64_t slab_base_offset = 2 * NVM_SLAB_SIZE;
    const SizeClassID sc_id = SC_64B;
    
    // NEW: 根据偏移量计算指针
    void* obj_ptr = (void*)((char*)mock_nvm_base + obj_offset);

    // --- 执行恢复 ---
    // MODIFIED: 调用新接口并检查int返回码
    int result = nvm_allocator_restore_allocation(g_allocator, obj_ptr, obj_size);
    TEST_ASSERT_EQUAL_INT(0, result);

    // --- 白盒验证 --- (内部状态验证逻辑不变)
    NvmSlab* restored_slab = g_allocator->slab_lists[sc_id];
    TEST_ASSERT_NOT_NULL(restored_slab);
    TEST_ASSERT_EQUAL_UINT64(slab_base_offset, restored_slab->nvm_base_offset);
    // ... 其他验证不变
    uint32_t block_idx = (obj_offset - slab_base_offset) / restored_slab->block_size;
    TEST_ASSERT_TRUE(IS_BIT_SET(restored_slab->bitmap, block_idx));
}

/**
 * @brief 测试在已存在的Slab中恢复第二个对象。
 */
void test_restore_second_object_in_existing_slab(void) {
    // 先恢复第一个对象，创建Slab
    // MODIFIED: 调用新接口
    TEST_ASSERT_EQUAL_INT(0, nvm_allocator_restore_allocation(g_allocator, mock_nvm_base, 32));
    
    // 现在恢复同一Slab中的第二个对象
    const uint64_t obj_offset = 128;
    const size_t obj_size = 32;
    // NEW: 计算指针
    void* obj_ptr = (void*)((char*)mock_nvm_base + obj_offset);

    // --- 执行恢复 ---
    // MODIFIED: 调用新接口并检查int返回码
    int result = nvm_allocator_restore_allocation(g_allocator, obj_ptr, obj_size);
    TEST_ASSERT_EQUAL_INT(0, result);

    // --- 白盒验证 --- (内部状态验证逻辑不变)
    NvmSlab* slab = g_allocator->slab_lists[SC_32B];
    TEST_ASSERT_EQUAL_UINT32(2, slab->allocated_block_count);
    TEST_ASSERT_TRUE(IS_BIT_SET(slab->bitmap, 0));
    TEST_ASSERT_TRUE(IS_BIT_SET(slab->bitmap, 4));
}

/**
 * @brief 测试恢复一个对象，其Slab正好是整个空闲空间的头部。
 */
void test_restore_object_at_head_of_space(void) {
    // MODIFIED: 调用新接口
    TEST_ASSERT_EQUAL_INT(0, nvm_allocator_restore_allocation(g_allocator, mock_nvm_base, 16));
    
    // 白盒验证不变
    FreeSegmentNode* head = g_allocator->space_manager->head;
    TEST_ASSERT_EQUAL_UINT64(NVM_SLAB_SIZE, head->nvm_offset);
}

/**
 * @brief 测试恢复一个对象，其Slab正好是整个空闲空间的尾部。
 */
void test_restore_object_at_tail_of_space(void) {
    const uint64_t slab_base_offset = (NUM_SLABS - 1) * NVM_SLAB_SIZE;
    // NEW: 计算指针
    void* obj_ptr = (void*)((char*)mock_nvm_base + slab_base_offset);

    // MODIFIED: 调用新接口
    TEST_ASSERT_EQUAL_INT(0, nvm_allocator_restore_allocation(g_allocator, obj_ptr, 16));

    // 白盒验证不变
    FreeSegmentNode* head = g_allocator->space_manager->head;
    TEST_ASSERT_EQUAL_UINT64(slab_base_offset, head->size);
    TEST_ASSERT_NULL(head->next);
}

/**
 * @brief 测试恢复流程中的错误处理路径。
 */
void test_restore_error_handling(void) {
    // MODIFIED: 所有失败断言都检查 int -1
    // 1. 无效参数
    TEST_ASSERT_EQUAL_INT(-1, nvm_allocator_restore_allocation(NULL, mock_nvm_base, 10));
    TEST_ASSERT_EQUAL_INT(-1, nvm_allocator_restore_allocation(g_allocator, NULL, 10));
    TEST_ASSERT_EQUAL_INT(-1, nvm_allocator_restore_allocation(g_allocator, mock_nvm_base, 0));

    // 2. 恢复一个大对象 (不支持)
    TEST_ASSERT_EQUAL_INT(-1, nvm_allocator_restore_allocation(g_allocator, mock_nvm_base, 4096 + 1));

    // 3. 恢复一个与已存在Slab尺寸冲突的对象
    nvm_allocator_restore_allocation(g_allocator, mock_nvm_base, 16);
    void* conflict_ptr = (void*)((char*)mock_nvm_base + 32);
    TEST_ASSERT_EQUAL_INT(-1, nvm_allocator_restore_allocation(g_allocator, conflict_ptr, 32));

    // 4. 恢复一个位于已被占用的空间中的对象
    void* occupied_ptr = (void*)((char*)mock_nvm_base + 64);
    TEST_ASSERT_EQUAL_INT(-1, nvm_allocator_restore_allocation(g_allocator, occupied_ptr, 64));
}

// ... (以下压力测试部分保持与上一轮相似的修改逻辑)
// ============================================================================
//                          压力测试及辅助函数
// ============================================================================

// 用于定义压力测试场景的辅助结构体
typedef struct {
    uint64_t    slab_base_offset;
    SizeClassID sc_id;
    uint32_t    block_size;
    int         num_objects_to_restore;
} StressTestSlabInfo;

// 辅助函数：恢复单个Slab中的所有指定对象
// MODIFIED: 此函数已更新以使用新的指针式API
static void restore_single_slab_for_stress_test(const StressTestSlabInfo* info) {
    for (int i = 0; i < info->num_objects_to_restore; ++i) {
        // 在Slab内部分散地恢复对象
        uint64_t block_offset_in_slab = (uint64_t)i * (info->block_size + 7);
        uint64_t obj_offset = info->slab_base_offset + block_offset_in_slab;

        // 确保对象不会超出Slab边界
        if (obj_offset + info->block_size > info->slab_base_offset + NVM_SLAB_SIZE) {
            continue;
        }
        
        // NEW: 根据偏移量计算指针
        void* obj_ptr = (void*)((char*)mock_nvm_base + obj_offset);

        // MODIFIED: 调用新接口并检查int返回码
        int result = nvm_allocator_restore_allocation(g_allocator, obj_ptr, info->block_size);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Failed to restore object during stress test");
    }
}

// 辅助函数：验证单个Slab的恢复后状态 (此函数无需修改)
static void verify_restored_slab(const StressTestSlabInfo* info) {
    NvmSlab* slab = slab_hashtable_lookup(g_allocator->slab_lookup_table, info->slab_base_offset);
    TEST_ASSERT_NOT_NULL(slab);
    
    // 验证Slab基本信息
    TEST_ASSERT_EQUAL_UINT64(info->slab_base_offset, slab->nvm_base_offset);
    TEST_ASSERT_EQUAL_UINT8(info->sc_id, slab->size_type_id);
    
    // 验证分配计数 (通过重新扫描位图来得到精确计数值)
    uint32_t actual_blocks_set = 0;
    for (uint32_t i = 0; i < slab->total_block_count; ++i) {
        if (IS_BIT_SET(slab->bitmap, i)) {
            actual_blocks_set++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(actual_blocks_set, slab->allocated_block_count);
    TEST_ASSERT_GREATER_THAN_INT32(0, slab->allocated_block_count);
}


/**
 * @brief 压力测试：恢复多个不同尺寸的Slab和大量对象
 */
void test_restore_multiple_slabs_and_stress(void) {
    // 1. --- 场景定义 ---
    StressTestSlabInfo test_scenario[] = {
        { .slab_base_offset = 1 * NVM_SLAB_SIZE, .sc_id = SC_16B,  .block_size = 16,   .num_objects_to_restore = 2000 },
        { .slab_base_offset = 4 * NVM_SLAB_SIZE, .sc_id = SC_128B, .block_size = 128,  .num_objects_to_restore = 1000 },
        { .slab_base_offset = 8 * NVM_SLAB_SIZE, .sc_id = SC_4K,   .block_size = 4096, .num_objects_to_restore = 511 }
    };
    const int num_scenarios = sizeof(test_scenario) / sizeof(test_scenario[0]);

    // 2. --- 执行恢复 --- (调用已修改的辅助函数)
    for (int i = 0; i < num_scenarios; ++i) {
        restore_single_slab_for_stress_test(&test_scenario[i]);
    }

    // 3. --- 白盒验证 ---
    // 3a. 验证Allocator顶层状态
    TEST_ASSERT_EQUAL_UINT32(num_scenarios, g_allocator->slab_lookup_table->count);
    TEST_ASSERT_NOT_NULL(g_allocator->slab_lists[SC_16B]);
    TEST_ASSERT_NOT_NULL(g_allocator->slab_lists[SC_128B]);
    TEST_ASSERT_NOT_NULL(g_allocator->slab_lists[SC_4K]);

    // 3b. 逐个验证每个Slab的状态
    for (int i = 0; i < num_scenarios; ++i) {
        verify_restored_slab(&test_scenario[i]);
    }

    // 3c. 验证空间管理器的碎片化状态
    // NEW: 这是完整的验证逻辑
    FreeSegmentNode* current = g_allocator->space_manager->head;

    // 验证第1个空闲块: [0, 1 * NVM_SLAB_SIZE]
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_UINT64(0 * NVM_SLAB_SIZE, current->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(1 * NVM_SLAB_SIZE, current->size);
    current = current->next;

    // 验证第2个空闲块: [2 * NVM_SLAB_SIZE, 2 * NVM_SLAB_SIZE]
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_UINT64(2 * NVM_SLAB_SIZE, current->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(2 * NVM_SLAB_SIZE, current->size);
    current = current->next;

    // 验证第3个空闲块: [5 * NVM_SLAB_SIZE, 3 * NVM_SLAB_SIZE]
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_UINT64(5 * NVM_SLAB_SIZE, current->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(3 * NVM_SLAB_SIZE, current->size);
    current = current->next;

    // 验证第4个空闲块: [9 * NVM_SLAB_SIZE, 1 * NVM_SLAB_SIZE]
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_UINT64(9 * NVM_SLAB_SIZE, current->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(1 * NVM_SLAB_SIZE, current->size);
    current = current->next;
    
    // 确认链表已结束
    TEST_ASSERT_NULL(current);
}


// ============================================================================
//                          测试执行入口
// ============================================================================
int main(void) {
    UNITY_BEGIN();

    // 运行专门为恢复逻辑编写的测试
    RUN_TEST(test_restore_first_object_in_new_slab);
    RUN_TEST(test_restore_second_object_in_existing_slab);
    RUN_TEST(test_restore_object_at_head_of_space);
    RUN_TEST(test_restore_object_at_tail_of_space);
    RUN_TEST(test_restore_error_handling);
    
    // MODIFIED: 取消对压力测试的注释
    RUN_TEST(test_restore_multiple_slabs_and_stress); 

    return UNITY_END();
}