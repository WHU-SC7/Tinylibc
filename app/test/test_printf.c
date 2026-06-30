/* SPDX-License-Identifier: MIT
 *
 * printf / snprintf 全面功能测试
 *
 * 覆盖:
 *   - 基本格式: %d %s %c %x %p %f %u %ld %%
 *   - 宽度: %10d %20s 等
 *   - 左对齐: %-10d %-20s 等
 *   - 零填充: %010d %08x 等
 *   - 组合: %-20s %010d
 *   - 边界: NULL 字符串、零值、负数、最大值
 *
 * 用法: build/output/test_printf
 */

#include "core.h"
#include "tlibc_print.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"
#include "string.h"

/* ================================================================== */
/*  测试框架 — 用宏实现（避免 va_list 转发问题）                      */
/* ================================================================== */

#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_RESET   "\033[0m"

static int g_tests   = 0;
static int g_passed  = 0;
static int g_failed  = 0;

#define CHECK_SNPRINTF(label, expected, fmt, ...) do {                  \
    g_tests++;                                                          \
    char _buf[512];                                                     \
    int _ret = snprintf(_buf, sizeof(_buf) - 1, fmt, ##__VA_ARGS__);   \
    _buf[sizeof(_buf) - 1] = '\0';                                      \
    if (strcmp(_buf, expected) == 0 && (int)strlen(expected) == _ret) { \
        g_passed++;                                                     \
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s\n", label);   \
    } else {                                                            \
        g_failed++;                                                     \
        printf("  " COLOR_RED "FAIL" COLOR_RESET "  %s\n", label);     \
        printf("         expected: \"%s\"\n", expected);                \
        printf("         got:      \"%s\"\n", _buf);                    \
        printf("         ret=%d (expected len=%lu)\n",                  \
               _ret, (unsigned long)strlen(expected));                  \
    }                                                                   \
} while (0)

#define CHECK_RETVAL(label, expected_ret, fmt, ...) do {                \
    g_tests++;                                                          \
    char _rbuf[8];                                                      \
    int _rret = snprintf(_rbuf, 8, fmt, ##__VA_ARGS__);                \
    if (_rret == expected_ret) {                                        \
        g_passed++;                                                     \
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s\n", label);   \
    } else {                                                            \
        g_failed++;                                                     \
        printf("  " COLOR_RED "FAIL" COLOR_RESET "  %s\n", label);     \
        printf("         expected ret=%d, got=%d\n", expected_ret, _rret); \
    }                                                                   \
} while (0)

/* ================================================================== */
/*  测试组                                                             */
/* ================================================================== */

static void
test_basic_formats(void)
{
    printf("\n" COLOR_CYAN "=== 基本格式 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("%%d (正数)",       "42",           "%d", 42);
    CHECK_SNPRINTF("%%d (负数)",       "-42",          "%d", -42);
    CHECK_SNPRINTF("%%d (零)",         "0",            "%d", 0);
    CHECK_SNPRINTF("%%d (INT_MAX)",    "2147483647",   "%d", 2147483647);
    CHECK_SNPRINTF("%%d (INT_MIN)",    "-2147483648",  "%d", -2147483647 - 1);
    CHECK_SNPRINTF("%%ld (大正数)",    "1234567890",   "%ld", 1234567890L);
    CHECK_SNPRINTF("%%ld (大负数)",    "-1234567890",  "%ld", -1234567890L);

    CHECK_SNPRINTF("%%s (普通)",       "hello",        "%s", "hello");
    CHECK_SNPRINTF("%%s (空字符串)",   "",             "%s", "");
    CHECK_SNPRINTF("%%s (NULL)",       "(null)",       "%s", (char *)NULL);

    CHECK_SNPRINTF("%%c",              "A",            "%c", 'A');
    CHECK_SNPRINTF("%%c (换行符)",     "\n",           "%c", '\n');

    CHECK_SNPRINTF("%%%%",             "%",            "%%");
}

static void
test_width_align(void)
{
    printf("\n" COLOR_CYAN "=== 宽度与对齐 ===" COLOR_RESET "\n");

    /* 整数右对齐 */
    CHECK_SNPRINTF("%%10d (右对齐)",   "        42",   "%10d", 42);
    CHECK_SNPRINTF("%%10d (负数)",     "       -42",   "%10d", -42);
    CHECK_SNPRINTF("%%1d (宽度=1)",    "42",           "%1d", 42);
    CHECK_SNPRINTF("%%10d (零)",       "         0",   "%10d", 0);

    /* 整数左对齐 */
    CHECK_SNPRINTF("%%-10d (左对齐)",  "42        ",   "%-10d", 42);
    CHECK_SNPRINTF("%%-10d (负数)",    "-42       ",   "%-10d", -42);

    /* 字符串对齐 */
    CHECK_SNPRINTF("%%10s (右对齐)",   "       abc",   "%10s", "abc");
    CHECK_SNPRINTF("%%-10s (左对齐)",  "abc       ",   "%-10s", "abc");
    CHECK_SNPRINTF("%%-15s (IP 对齐)", "10.0.0.1       ", "%-15s", "10.0.0.1");

    /* 字符串宽度小于内容 */
    CHECK_SNPRINTF("%%2s (宽度<内容)", "abcdef",       "%2s", "abcdef");

    /* 字符对齐 */
    CHECK_SNPRINTF("%%5c (右对齐)",   "    X",        "%5c", 'X');
    CHECK_SNPRINTF("%%-5c (左对齐)",  "X    ",        "%-5c", 'X');
}

static void
test_zero_pad(void)
{
    printf("\n" COLOR_CYAN "=== 零填充 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("%%010d",           "0000000042",   "%010d", 42);
    CHECK_SNPRINTF("%%010d (负数)",    "-000000042",   "%010d", -42);
    CHECK_SNPRINTF("%%010d (零)",      "0000000000",   "%010d", 0);
    CHECK_SNPRINTF("%%010d (大数)",    "2147483647",   "%010d", 2147483647);
    CHECK_SNPRINTF("%%012d (零+大数)", "002147483647", "%012d", 2147483647);
    CHECK_SNPRINTF("%%010d (大负数)",  "-2147483648",  "%010d", -2147483647 - 1);
    CHECK_SNPRINTF("%%013d (零+大负)", "-002147483648", "%013d", -2147483647 - 1);

    CHECK_SNPRINTF("%%08x",            "0000002a",     "%08x", 42);
    CHECK_SNPRINTF("%%08x (零)",       "00000000",     "%08x", 0);
    CHECK_SNPRINTF("%%08x (全f)",      "0000ffff",     "%08x", 0xffff);

    /* 零填充 + 左对齐 = 忽略零填充 */
    CHECK_SNPRINTF("%%-010d (零+左)",  "42        ",   "%-010d", 42);
}

static void
test_hex_and_ptr(void)
{
    printf("\n" COLOR_CYAN "=== 十六进制与指针 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("%%x",              "2a",           "%x", 42);
    CHECK_SNPRINTF("%%x (255)",        "ff",           "%x", 255);
    CHECK_SNPRINTF("%%x (零)",         "0",            "%x", 0);
    CHECK_SNPRINTF("%%x (大数)",       "7fffffff",     "%x", 0x7fffffff);

    CHECK_SNPRINTF("%%p (零)",         "0x0",          "%p", (void *)0);
    CHECK_SNPRINTF("%%p (非零)",       "0x7f",         "%p", (void *)0x7f);
}

static void
test_unsigned(void)
{
    printf("\n" COLOR_CYAN "=== 无符号整数 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("%%u",              "42",           "%u", 42);
    CHECK_SNPRINTF("%%u (0)",          "0",            "%u", 0);
    CHECK_SNPRINTF("%%u (大)",         "4294967295",   "%u", 0xffffffff);
    CHECK_SNPRINTF("%%10u",            "        42",   "%10u", 42);
}

static void
test_float(void)
{
    printf("\n" COLOR_CYAN "=== 浮点数 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("%%f (整数)",       "42.000000",    "%f", 42.0);
    CHECK_SNPRINTF("%%f (小数)",       "3.141593",     "%f", 3.1415926535);
    CHECK_SNPRINTF("%%f (负数)",       "-3.140000",    "%f", -3.14);
    CHECK_SNPRINTF("%%f (零)",         "0.000000",     "%f", 0.0);

    CHECK_SNPRINTF("%%10f (右对齐)",   " 42.000000",   "%10f", 42.0);
    CHECK_SNPRINTF("%%-10f (左对齐)",  "42.000000 ",   "%-10f", 42.0);
}

static void
test_combined(void)
{
    printf("\n" COLOR_CYAN "=== 组合格式 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("多个参数",
                   "42 + hello = 42",
                   "%d + %s = %d", 42, "hello", 42);

    CHECK_SNPRINTF("对齐组合",
                   "    42hello     ",
                   "%6d%-10s", 42, "hello");

    CHECK_SNPRINTF("混合类型",
                   "str=hello, int=255, hex=ff",
                   "str=%s, int=%d, hex=%x",
                   "hello", 255, 255);

    CHECK_SNPRINTF("零填充+十六进制",
                   "0x000000ff",
                   "0x%08x", 255);

    CHECK_SNPRINTF("多%%",
                   "100% done",
                   "100%% done");
}

static void
test_retval(void)
{
    printf("\n" COLOR_CYAN "=== snprintf 返回值 ===" COLOR_RESET "\n");

    CHECK_RETVAL("短缓冲区返回值",       10, "%d", 1234567890);
    CHECK_RETVAL("精确匹配缓冲区",       3,  "%d", 123);
    CHECK_RETVAL("空字符串返回值",       0,  "%s", "");
    CHECK_RETVAL("格式字符返回值",       1,  "%%");
}

static void
test_snprintf_truncation(void)
{
    printf("\n" COLOR_CYAN "=== snprintf 截断 ===" COLOR_RESET "\n");

    char buf[8];
    int ret;

    memset(buf, 0xAA, sizeof(buf));
    ret = snprintf(buf, 4, "%s", "hello world");
    buf[3] = '\0';  /* snprintf 应已写入终止符 */

    int trunc_ok = (strcmp(buf, "hel") == 0 && ret == 11);
    g_tests++;
    if (trunc_ok) {
        g_passed++;
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  snprintf 截断 (buf=4, expected=11)\n");
    } else {
        g_failed++;
        printf("  " COLOR_RED "FAIL" COLOR_RESET "  snprintf 截断\n");
        printf("         buf=\"%s\", ret=%d (expected \"hel\", ret=11)\n", buf, ret);
    }

    /* size=0 应只返回长度,不写任何内容 */
    memset(buf, 0xAA, 8);
    ret = snprintf(NULL, 0, "%s", "hello");
    int zero_ok = (ret == 5);
    g_tests++;
    if (zero_ok) {
        g_passed++;
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  snprintf size=0 (ret=5)\n");
    } else {
        g_failed++;
        printf("  " COLOR_RED "FAIL" COLOR_RESET "  snprintf size=0\n");
        printf("         ret=%d (expected 5)\n", ret);
    }
}

static void
test_edge_cases(void)
{
    printf("\n" COLOR_CYAN "=== 边界情况 ===" COLOR_RESET "\n");

    /* 空格式字符串 */
    CHECK_SNPRINTF("空格式",            "",             "");
    CHECK_SNPRINTF("纯文本",            "hello",        "hello");

    /* INT_MIN 的特殊处理: -(-2147483648) 会溢出，需正确处理 */
    CHECK_SNPRINTF("INT_MIN",           "-2147483648",  "%d", -2147483647 - 1);

    /* 大宽度（不应崩） */
    /* %100s 预期 = 99 空格 + "x" = 100 字符，运行时构造以避免计数错误 */
    {
        char big_expected[128];
        memset(big_expected, ' ', 99);
        big_expected[99] = 'x';
        big_expected[100] = '\0';
        CHECK_SNPRINTF("%%100s (大宽度)", big_expected, "%100s", "x");
    }

    /* %% 组合 */
    CHECK_SNPRINTF("%%+文本",           "%hello",       "%%hello");
    CHECK_SNPRINTF("文本+%%",           "hello%",       "hello%%");
}

static void
test_precision_s(void)
{
    printf("\n" COLOR_CYAN "=== %%.*s 精度格式 ===" COLOR_RESET "\n");

    CHECK_SNPRINTF("%%.*s, len=3",          "hel",          "%.*s", 3, "hello");
    CHECK_SNPRINTF("%%.*s, len=5",          "hello",        "%.*s", 5, "hello");
    CHECK_SNPRINTF("%%.*s, len=0",          "",             "%.*s", 0, "hello");
    CHECK_SNPRINTF("%%.*s, len>strlen",     "hello",        "%.*s", 10, "hello");
    CHECK_SNPRINTF("%%.*s, NULL, len=3",    "(nu",          "%.*s", 3, (char *)NULL);
    CHECK_SNPRINTF("%%.*s, NULL, len=6",    "(null)",       "%.*s", 6, (char *)NULL);
    CHECK_SNPRINTF("%%.*s, NULL, len=0",    "",             "%.*s", 0, (char *)NULL);
    CHECK_SNPRINTF("%%.*s, len=1",          "a",            "%.*s", 1, "abc");
    CHECK_SNPRINTF("%%.*s, len=100",        "abc",          "%.*s", 100, "abc");

    CHECK_SNPRINTF("%%10.*s, len=3",        "       hel",   "%10.*s", 3, "hello");
    CHECK_SNPRINTF("%%-10.*s, len=3",       "hel       ",   "%-10.*s", 3, "hello");
    CHECK_SNPRINTF("%%5.*s, len=5",         "hello",        "%5.*s", 5, "hello");
    CHECK_SNPRINTF("%%-5.*s, len=3",        "hel  ",        "%-5.*s", 3, "hello");
}

/* ================================================================== */
/*  主函数                                                             */
/* ================================================================== */

int main(int argc, char *argv[])
{
    printf(COLOR_YELLOW "========================================\n" COLOR_RESET);
    printf(COLOR_YELLOW "  printf / snprintf 综合测试\n" COLOR_RESET);
    printf(COLOR_YELLOW "========================================\n" COLOR_RESET);

    test_basic_formats();
    test_width_align();
    test_zero_pad();
    test_hex_and_ptr();
    test_unsigned();
    test_float();
    test_combined();
    test_precision_s();
    test_retval();
    test_snprintf_truncation();
    test_edge_cases();

    printf("\n" COLOR_YELLOW "========================================\n" COLOR_RESET);
    printf("  总计: %d  通过: " COLOR_GREEN "%d" COLOR_RESET "  失败: ",
           g_tests, g_passed);
    if (g_failed > 0)
        printf(COLOR_RED "%d" COLOR_RESET, g_failed);
    else
        printf("%d", g_failed);
    printf("\n");
    printf(COLOR_YELLOW "========================================\n" COLOR_RESET);

    return g_failed > 0 ? 1 : 0;
}
