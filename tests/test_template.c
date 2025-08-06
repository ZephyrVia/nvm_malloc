#include "unity.h"      // 包含 Unity 测试框架的头文件
#include "template.h"   // 包含我们要测试的模块的头文件

// setUp 函数会在每个测试用例运行前被调用
void setUp(void) {
    // 这里是空的，因为我们的简单测试不需要每次都进行准备工作
}

// tearDown 函数会在每个测试用例运行后被调用
void tearDown(void) {
    // 这里是空的，因为我们的简单测试不需要每次都进行清理工作
}

// --- 测试用例从这里开始 ---

// 测试 add 函数的基本功能
void test_add_function(void) {
    // 测试正数相加
    TEST_ASSERT_EQUAL_INT(5, add(2, 3));
    
    // 测试包含负数的情况
    TEST_ASSERT_EQUAL_INT(-1, add(2, -3));
    
    // 测试零
    TEST_ASSERT_EQUAL_INT(0, add(0, 0));
}

// 测试 subtract 函数的基本功能
void test_subtract_function(void) {
    // 测试正数相减
    TEST_ASSERT_EQUAL_INT(-1, subtract(2, 3));
    
    // 测试减去一个负数
    TEST_ASSERT_EQUAL_INT(5, subtract(2, -3));

    // 测试结果为零
    TEST_ASSERT_EQUAL_INT(0, subtract(10, 10));
}


// --- main 函数是所有测试的入口 ---
int main(void) {
    UNITY_BEGIN(); // 初始化 Unity 测试环境

    // 在这里按顺序运行我们写好的测试用例
    RUN_TEST(test_add_function);
    RUN_TEST(test_subtract_function);

    return UNITY_END(); // 结束测试并报告最终结果
}