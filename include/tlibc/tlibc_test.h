#ifndef __TLIBC_TEST_H
#define __TLIBC_TEST_H

#include "core.h"    /* __printf */
#include "string.h"  /* strcmp */

/*
 * Tinylibc 测试框架
 *
 * 用法：每个 .c 文件是一个独立的测试 binary。
 * 每个测试用例是一个 void func(void) 函数。
 *
 *   void test_strlen_basic(void) {
 *       TEST_START("strlen with empty string");
 *       TEST_ASSERT_EQ(strlen(""), 0, "%d");
 *       TEST_PASS();
 *   }
 *
 *   int main(void) {
 *       TEST_BEGIN("string tests");
 *       test_strlen_basic();
 *       return TEST_END();
 *   }
 */

/* 内部计数器 —— 每个测试 binary 在 main 之前定义一次 */
extern int __test_passed;
extern int __test_failed;

/* 在测试 .c 中定义计数器（放在 main 之前的文件作用域） */
#define TEST_DEFINE_COUNTERS() \
    int __test_passed; \
    int __test_failed

#define _T_GREEN  "\033[32m"
#define _T_RED    "\033[31m"
#define _T_CYAN   "\033[36m"
#define _T_RESET  "\033[0m"

/* 开始一个命名的测试用例。断言失败时会 return 提前退出该用例。 */
#define TEST_START(name) \
    do { \
        __printf("  " _T_CYAN "%s" _T_RESET " ... ", name); \
    } while (0)

/* 断言条件为真 */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            __printf(_T_RED "FAIL" _T_RESET "\n    %s:%d: %s\n", \
                     __FILE__, __LINE__, msg); \
            __test_failed++; \
            return; \
        } \
    } while (0)

/* 断言两个整数相等。fmt 是 __printf 的格式占位符，如 "%d" "%lx" */
#define TEST_ASSERT_EQ(actual, expected, fmt) \
    do { \
        if ((actual) != (expected)) { \
            __printf(_T_RED "FAIL" _T_RESET \
                     "\n    %s:%d: expected " fmt ", got " fmt "\n", \
                     __FILE__, __LINE__, (expected), (actual)); \
            __test_failed++; \
            return; \
        } \
    } while (0)

/* 断言两个字符串相等 */
#define TEST_ASSERT_STR_EQ(actual, expected) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            __printf(_T_RED "FAIL" _T_RESET \
                     "\n    %s:%d: expected \"%s\", got \"%s\"\n", \
                     __FILE__, __LINE__, (expected), (actual)); \
            __test_failed++; \
            return; \
        } \
    } while (0)

/* 标记当前用例通过 */
#define TEST_PASS() \
    do { \
        __printf(_T_GREEN "PASS" _T_RESET "\n"); \
        __test_passed++; \
    } while (0)

/* 标记当前用例失败（手动） */
#define TEST_FAIL(msg) \
    do { \
        __printf(_T_RED "FAIL" _T_RESET "\n    %s:%d: %s\n", \
                 __FILE__, __LINE__, msg); \
        __test_failed++; \
    } while (0)

/* main 开头 —— 输出 suite 名称 */
#define TEST_BEGIN(suite_name) \
    do { \
        __test_passed = 0; \
        __test_failed = 0; \
        __printf("=== " _T_CYAN "%s" _T_RESET " ===\n", suite_name); \
    } while (0)

/* main 结尾 —— 输出汇总，返回 0（全 PASS）或 1（有 FAIL） */
#define TEST_END() \
    ( \
        __printf("  %d/%d passed", \
                 __test_passed, __test_passed + __test_failed), \
        __test_failed > 0 \
            ? (__printf(", " _T_RED "%d failed" _T_RESET "\n", \
                        __test_failed), \
               1) \
            : (__printf("\n"), \
               0) \
    )

#endif /* __TLIBC_TEST_H */
