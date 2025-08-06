#include "unity.h"
#include "NvmDefs.h"
#include "NvmSpaceManager.h"

// 我们直接包含 .c 文件，以便在需要时可以访问 static 函数进行白盒测试，
// 并且简化了编译过程。这是 Unity 测试框架的常见模式。
#include "NvmSpaceManager.c" 

#include <stdlib.h>

// 为测试环境定义常量
// **修正: 将 NVM_CHUNK_SIZE 改为 SLAB_SIZE，与 NvmDefs.h 中的定义保持一致**
#define TOTAL_TEST_SIZE (10 * NVM_SLAB_SIZE) 
#define NUM_CHUNKS (10)

// setUp 和 tearDown 在本测试文件中可以是空的，因为每个测试
// 都会创建和销毁自己的 Manager 实例，以保证测试的完全隔离。
void setUp(void) {}
void tearDown(void) {}

// ============================================================================
//                          辅助函数
// =================================A===========================================

// 辅助函数，用于验证链表中只有一个节点，并且其属性正确
static void verify_single_node_state(FreeSpaceManager* manager, uint64_t expected_offset, uint64_t expected_size) {
    TEST_ASSERT_NOT_NULL_MESSAGE(manager->head, "Manager head should not be NULL.");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(manager->head, manager->tail, "Head and tail should be the same for a single node list.");
    TEST_ASSERT_NULL_MESSAGE(manager->head->prev, "Single node's prev should be NULL.");
    TEST_ASSERT_NULL_MESSAGE(manager->head->next, "Single node's next should be NULL.");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(expected_offset, manager->head->nvm_offset, "Node offset mismatch.");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(expected_size, manager->head->size, "Node size mismatch.");
}


// ============================================================================
//                          测试用例
// ============================================================================

/**
 * @brief 测试 Space Manager 的创建和销毁。
 */
void test_space_manager_creation_and_destruction(void) {
    // --- 子测试 1: 正常创建 ---
    FreeSpaceManager* manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(manager, "space_manager_create should succeed with valid parameters.");
    
    // 验证初始状态：应该只有一个节点，代表全部空闲空间
    verify_single_node_state(manager, 0, TOTAL_TEST_SIZE);
    
    space_manager_destroy(manager);
    manager = NULL; // 好习惯

    // --- 子测试 2: 参数无效（空间太小）---
    // **修正: 将 NVM_CHUNK_SIZE 改为 NVM_SLAB_SIZE**
    manager = space_manager_create(NVM_SLAB_SIZE - 1, 0);
    TEST_ASSERT_NULL_MESSAGE(manager, "space_manager_create should fail if total size is less than a chunk.");

    // --- 子测试 3: 销毁 NULL 指针 ---
    // 这个调用不应该导致任何崩溃
    space_manager_destroy(NULL);
}


void test_alloc_and_free_with_merging(void) {
    FreeSpaceManager* manager;
    uint64_t c0, c1, c2, c3, c4, offset_after_c2, offset_after_c3;

    // ========================================================================
    // --- 子测试 1: 无合并 ---
    // ========================================================================
    manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    c0 = space_manager_alloc_slab(manager);
    c1 = space_manager_alloc_slab(manager);
    c2 = space_manager_alloc_slab(manager);
    offset_after_c2 = c2 + NVM_SLAB_SIZE;
    
    // 释放 c1。它被 c0 和 c2 包围，无法合并。
    space_manager_free_slab(manager, c1);
    
    // 链表应为: [c1] -> [c3-c9]
    TEST_ASSERT_EQUAL_UINT64(c1, manager->head->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(NVM_SLAB_SIZE, manager->head->size);
    TEST_ASSERT_NOT_NULL(manager->head->next);
    TEST_ASSERT_EQUAL_UINT64(offset_after_c2, manager->head->next->nvm_offset);
    space_manager_destroy(manager);

    // ========================================================================
    // --- 子测试 2: 向前合并 ---
    // ========================================================================
    manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    c0 = space_manager_alloc_slab(manager);
    c1 = space_manager_alloc_slab(manager);
    
    space_manager_free_slab(manager, c1); // 先释放 c1
    space_manager_free_slab(manager, c0); // 再释放 c0，应与 c1 向前合并
    
    // 链表应为: [c0-c1] -> [c2-c9]
    TEST_ASSERT_EQUAL_UINT64(c0, manager->head->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(10 * NVM_SLAB_SIZE, manager->head->size);
    space_manager_destroy(manager);

    // ========================================================================
    // --- 子测试 3: 向后合并 ---
    // ========================================================================
    manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    c0 = space_manager_alloc_slab(manager);
    c1 = space_manager_alloc_slab(manager);
    c2 = space_manager_alloc_slab(manager);
    
    space_manager_free_slab(manager, c1); // 释放 c1，形成独立节点
    space_manager_free_slab(manager, c2); // 释放 c2，与后面的 [c3-c9] 合并
    
    // 链表应为: [c1] -> [c2-c9]
    // 此时再释放 c1，它并不会与 [c2-c9] 合并，因为我们是要测试 c1 和 c2 的合并
    // 让我们换个思路来测试
    space_manager_destroy(manager);
    
    manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    c0 = space_manager_alloc_slab(manager); // 墙
    c1 = space_manager_alloc_slab(manager);
    c2 = space_manager_alloc_slab(manager);
    c3 = space_manager_alloc_slab(manager); // 墙
    
    space_manager_free_slab(manager, c2); // 释放 c2
    // 链表: [c2] -> [c4-c9]
    space_manager_free_slab(manager, c1); // 释放 c1, 它应该和 [c2] 向后合并
    
    // 链表应为: [c1-c2] -> [c4-c9]
    TEST_ASSERT_EQUAL_UINT64(c1, manager->head->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(2 * NVM_SLAB_SIZE, manager->head->size);
    space_manager_destroy(manager);

    // ========================================================================
    // --- 子测试 4: 三向合并 ---
    // ========================================================================
    manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    c0 = space_manager_alloc_slab(manager);
    c1 = space_manager_alloc_slab(manager);
    c2 = space_manager_alloc_slab(manager);
    c3 = space_manager_alloc_slab(manager);
    c4 = space_manager_alloc_slab(manager); // 墙
    
    space_manager_free_slab(manager, c1); // 释放 c1
    space_manager_free_slab(manager, c3); // 释放 c3
    // 链表: [c1] -> [c3] -> [c5-c9]
    
    space_manager_free_slab(manager, c2); // 释放 c2, 连接 [c1] 和 [c3]
    
    // 链表应为: [c1-c3] -> [c5-c9]
    TEST_ASSERT_EQUAL_UINT64(c1, manager->head->nvm_offset);
    TEST_ASSERT_EQUAL_UINT64(3 * NVM_SLAB_SIZE, manager->head->size);
    TEST_ASSERT_NOT_NULL(manager->head->next);
    space_manager_destroy(manager);
}


/**
 * @brief 测试将所有块完全分配，然后再全部释放的完整周期。
 */
void test_full_allocation_and_deallocation_cycle(void) {
    FreeSpaceManager* manager = space_manager_create(TOTAL_TEST_SIZE, 0);
    TEST_ASSERT_NOT_NULL(manager);
    
    uint64_t* offsets = malloc(sizeof(uint64_t) * NUM_CHUNKS);
    TEST_ASSERT_NOT_NULL(offsets);

    // --- 1. 完全分配 ---
    for (int i = 0; i < NUM_CHUNKS; ++i) {
        offsets[i] = space_manager_alloc_slab(manager);
        TEST_ASSERT_NOT_EQUAL_MESSAGE((uint64_t)-1, offsets[i], "Allocation should succeed before space is exhausted.");
        // **修正: 将 NVM_CHUNK_SIZE 改为 NVM_SLAB_SIZE**
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(i * NVM_SLAB_SIZE, offsets[i], "Allocated offsets should be sequential.");
    }
    
    // --- 2. 验证空间已耗尽 ---
    TEST_ASSERT_NULL_MESSAGE(manager->head, "Manager should be empty after full allocation.");
    uint64_t extra_alloc = space_manager_alloc_slab(manager);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)-1, extra_alloc, "Allocation should fail when space is exhausted.");

    // --- 3. 完全释放 (以相反顺序，测试合并逻辑) ---
    for (int i = NUM_CHUNKS - 1; i >= 0; --i) {
        space_manager_free_slab(manager, offsets[i]);
    }

    // --- 4. 验证已完全恢复 ---
    verify_single_node_state(manager, 0, TOTAL_TEST_SIZE);
    
    // --- 5. 再次完全分配，验证可重复性 ---
    for (int i = 0; i < NUM_CHUNKS; ++i) {
        uint64_t offset = space_manager_alloc_slab(manager);
        TEST_ASSERT_NOT_EQUAL((uint64_t)-1, offset);
    }
    TEST_ASSERT_NULL_MESSAGE(manager->head, "Re-allocation should also exhaust the manager.");

    // 清理
    free(offsets);
    space_manager_destroy(manager);
}


// ============================================================================
//                          测试执行入口
// ============================================================================
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_space_manager_creation_and_destruction);
    RUN_TEST(test_alloc_and_free_with_merging);
    RUN_TEST(test_full_allocation_and_deallocation_cycle);

    return UNITY_END();
}