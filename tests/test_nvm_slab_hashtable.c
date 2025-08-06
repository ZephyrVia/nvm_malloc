#include "unity.h"
#include "NvmDefs.h"       // For NVM_SLAB_SIZE
#include "NvmSlab.h"       // For NvmSlab* type
#include "SlabHashTable.h"

// We directly include the .c file for white-box testing.
#include "SlabHashTable.c"

#include <stdlib.h>

// A prime number is a good choice for capacity to improve hash distribution.
#define TEST_HT_CAPACITY 17

// In C, we can't create real NvmSlab objects easily without their own create functions.
// For testing the hash table, we only need unique pointer values.
// We can create mock slab pointers by casting integer addresses.
#define MOCK_SLAB_1 ((NvmSlab*)0x1000)
#define MOCK_SLAB_2 ((NvmSlab*)0x2000)
#define MOCK_SLAB_3 ((NvmSlab*)0x3000)

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
//                          测试用例
// ============================================================================

/**
 * @brief 测试哈希表的创建和销毁。
 */
void test_hashtable_creation_and_destruction(void) {
    // --- 子测试 1: 正常创建 ---
    SlabHashTable* table = slab_hashtable_create(TEST_HT_CAPACITY);
    TEST_ASSERT_NOT_NULL_MESSAGE(table, "Create should succeed with a valid capacity.");
    TEST_ASSERT_NOT_NULL_MESSAGE(table->buckets, "Buckets array should be allocated.");
    TEST_ASSERT_EQUAL_UINT32(TEST_HT_CAPACITY, table->capacity);
    TEST_ASSERT_EQUAL_UINT32(0, table->count);

    // 白盒测试: 确认所有桶都已初始化为 NULL
    for (uint32_t i = 0; i < table->capacity; ++i) {
        TEST_ASSERT_NULL(table->buckets[i]);
    }
    
    // --- 子测试 2: 正常销毁 ---
    slab_hashtable_destroy(table);

    // --- 子测试 3: 无效参数创建 ---
    table = slab_hashtable_create(0);
    TEST_ASSERT_NULL_MESSAGE(table, "Create should fail with zero capacity.");

    // --- 子测试 4: 销毁 NULL 指针 ---
    // 此调用不应导致任何崩溃
    slab_hashtable_destroy(NULL);
}

/**
 * @brief 测试基本的插入和查找功能。
 */
void test_hashtable_insert_and_lookup(void) {
    SlabHashTable* table = slab_hashtable_create(TEST_HT_CAPACITY);
    uint64_t key1 = 0 * NVM_SLAB_SIZE;
    uint64_t key2 = 1 * NVM_SLAB_SIZE;
    uint64_t non_existent_key = 99 * NVM_SLAB_SIZE;

    // --- 子测试 1: 成功插入 ---
    int ret = slab_hashtable_insert(table, key1, MOCK_SLAB_1);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT32(1, table->count);

    ret = slab_hashtable_insert(table, key2, MOCK_SLAB_2);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT32(2, table->count);

    // --- 子测试 2: 成功查找 ---
    NvmSlab* found_slab = slab_hashtable_lookup(table, key1);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_1, found_slab);

    found_slab = slab_hashtable_lookup(table, key2);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_2, found_slab);

    // --- 子测试 3: 查找不存在的键 ---
    found_slab = slab_hashtable_lookup(table, non_existent_key);
    TEST_ASSERT_NULL(found_slab);

    // --- 子测试 4: 插入重复的键 ---
    ret = slab_hashtable_insert(table, key1, MOCK_SLAB_3);
    TEST_ASSERT_EQUAL_INT(-1, ret);      // 应该失败
    TEST_ASSERT_EQUAL_UINT32(2, table->count); // 计数不应增加
    // 验证旧值未被覆盖
    found_slab = slab_hashtable_lookup(table, key1);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_1, found_slab); 

    slab_hashtable_destroy(table);
}

/**
 * @brief 专门测试哈希冲突时的行为（拉链法）。
 */
void test_hashtable_collisions(void) {
    SlabHashTable* table = slab_hashtable_create(TEST_HT_CAPACITY);

    // 构造两个会产生哈希冲突的键
    // hash(key) = (key / NVM_SLAB_SIZE) % capacity
    // (0 / NVM_SLAB_SIZE) % 17 = 0
    // (17 / NVM_SLAB_SIZE) % 17 = 0
    uint64_t key1 = 0 * NVM_SLAB_SIZE;
    uint64_t key_collides_with_1 = TEST_HT_CAPACITY * NVM_SLAB_SIZE;
    
    // 1. 插入这两个冲突的键
    slab_hashtable_insert(table, key1, MOCK_SLAB_1);
    slab_hashtable_insert(table, key_collides_with_1, MOCK_SLAB_2);
    TEST_ASSERT_EQUAL_UINT32(2, table->count);
    
    // 2. 白盒测试: 验证它们是否在同一个桶的链表中
    uint32_t bucket_index = hash_function(table, key1);
    SlabHashNode* head_node = table->buckets[bucket_index];
    TEST_ASSERT_NOT_NULL(head_node);

    // 因为是头插法，后插入的 MOCK_SLAB_2 应该在链表头部
    TEST_ASSERT_EQUAL_UINT64(key_collides_with_1, head_node->nvm_offset);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_2, head_node->slab_ptr);
    TEST_ASSERT_NOT_NULL(head_node->next);
    
    // 先插入的 MOCK_SLAB_1 应该是第二个节点
    SlabHashNode* second_node = head_node->next;
    TEST_ASSERT_EQUAL_UINT64(key1, second_node->nvm_offset);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_1, second_node->slab_ptr);
    TEST_ASSERT_NULL(second_node->next);

    // 3. 查找这两个冲突的键
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_1, slab_hashtable_lookup(table, key1));
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_2, slab_hashtable_lookup(table, key_collides_with_1));

    slab_hashtable_destroy(table);
}


/**
 * @brief 测试移除功能，包括处理冲突的情况。
 */
void test_hashtable_remove(void) {
    SlabHashTable* table = slab_hashtable_create(TEST_HT_CAPACITY);
    uint64_t key1 = 0 * NVM_SLAB_SIZE;
    uint64_t key2 = TEST_HT_CAPACITY * NVM_SLAB_SIZE; // 与 key1 冲突
    uint64_t key3 = 1 * NVM_SLAB_SIZE;               // 不冲突

    slab_hashtable_insert(table, key1, MOCK_SLAB_1);
    slab_hashtable_insert(table, key2, MOCK_SLAB_2);
    slab_hashtable_insert(table, key3, MOCK_SLAB_3);
    TEST_ASSERT_EQUAL_UINT32(3, table->count);

    // --- 子测试 1: 移除不存在的键 ---
    NvmSlab* removed_slab = slab_hashtable_remove(table, 99 * NVM_SLAB_SIZE);
    TEST_ASSERT_NULL(removed_slab);
    TEST_ASSERT_EQUAL_UINT32(3, table->count);

    // --- 子测试 2: 移除一个没有冲突的键 ---
    removed_slab = slab_hashtable_remove(table, key3);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_3, removed_slab);
    TEST_ASSERT_EQUAL_UINT32(2, table->count);
    TEST_ASSERT_NULL(slab_hashtable_lookup(table, key3)); // 确认已被移除

    // --- 子测试 3: 移除一个有冲突的链表头节点 ---
    removed_slab = slab_hashtable_remove(table, key2);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_2, removed_slab);
    TEST_ASSERT_EQUAL_UINT32(1, table->count);
    TEST_ASSERT_NULL(slab_hashtable_lookup(table, key2));
    // 确认链表中的另一个节点还在
    TEST_ASSERT_NOT_NULL(slab_hashtable_lookup(table, key1)); 

    // --- 子测试 4: 移除最后一个节点 ---
    removed_slab = slab_hashtable_remove(table, key1);
    TEST_ASSERT_EQUAL_PTR(MOCK_SLAB_1, removed_slab);
    TEST_ASSERT_EQUAL_UINT32(0, table->count);
    TEST_ASSERT_NULL(slab_hashtable_lookup(table, key1));
    // 白盒检查: 对应的桶应该变回 NULL
    uint32_t bucket_index = hash_function(table, key1);
    TEST_ASSERT_NULL(table->buckets[bucket_index]);

    slab_hashtable_destroy(table);
}


// ============================================================================
//                          测试执行入口
// ============================================================================
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_hashtable_creation_and_destruction);
    RUN_TEST(test_hashtable_insert_and_lookup);
    RUN_TEST(test_hashtable_collisions);
    RUN_TEST(test_hashtable_remove);

    return UNITY_END();
}