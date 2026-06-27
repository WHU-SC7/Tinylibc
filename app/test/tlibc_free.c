/*
 * tlibc_free 单元测试
 *
 * 覆盖：
 *   基本 alloc/free / NULL 安全 / 哨兵值 / 多次分配
 *   大数据 / 循环复用 / malloc + tlibc_free 混合
 *
 * 构建 & 运行：
 *   tmake -b tlibc_free
 *   build/output/tlibc_free
 */

#include "core.h"
#include "pthread.h"
#include "errno.h"
#include "mempool.h"

/* 测试计数 */
static int passed, failed;
#define TEST(name)   do { __printf("  " name " ... "); } while(0)
#define PASS()       do { __printf("OK\n"); passed++; } while(0)
#define FAIL(reason) do { __printf("FAIL: %s\n", reason); failed++; } while(0)

#define N_SMALL  256
#define N_LARGE  32
#define LARGE_SZ (1024 * 1024)   /* 1 MiB */

/* ========= 1. 基本 alloc + free ========= */
static void test_basic_alloc_free(void)
{
    void *p;

    TEST("tlibc_malloc(64) + tlibc_free");
    p = tlibc_malloc(64);
    if (!p) { FAIL("alloc returned NULL"); return; }
    tlibc_free(p);
    PASS();

    TEST("tlibc_malloc(1) + tlibc_free");
    p = tlibc_malloc(1);
    if (!p) { FAIL("alloc returned NULL"); return; }
    tlibc_free(p);
    PASS();

    TEST("tlibc_malloc(0) -> 1 byte + tlibc_free");
    p = tlibc_malloc(0);
    if (!p) { FAIL("alloc returned NULL"); return; }
    tlibc_free(p);
    PASS();
}

/* ========= 2. 安全释放（NULL / 哨兵） ========= */
static void test_safe_free(void)
{
    TEST("tlibc_free(NULL) 不崩溃");
    tlibc_free(NULL);
    PASS();

    TEST("tlibc_free(MAP_FAILED) 不崩溃");
    tlibc_free(MAP_FAILED);
    PASS();

    TEST("tlibc_free((void*)-2) 不崩溃");
    tlibc_free((void *)-2);
    PASS();
}

/* ========= 3. 分配后写入数据再释放 ========= */
static void test_write_then_free(void)
{
    unsigned char *p;

    TEST("alloc 256 bytes, 写入 0xA5, free");
    p = (unsigned char *)tlibc_malloc(256);
    if (!p) { FAIL("alloc returned NULL"); return; }
    for (int i = 0; i < 256; i++) p[i] = 0xA5;
    for (int i = 0; i < 256; i++) {
        if (p[i] != 0xA5) { FAIL("data corruption before free"); return; }
    }
    tlibc_free(p);
    PASS();

    TEST("alloc 4K, 写入 0x5A, free");
    p = (unsigned char *)tlibc_malloc(4096);
    if (!p) { FAIL("alloc returned NULL"); return; }
    for (int i = 0; i < 4096; i++) p[i] = 0x5A;
    for (int i = 0; i < 4096; i++) {
        if (p[i] != 0x5A) { FAIL("data corruption before free"); return; }
    }
    tlibc_free(p);
    PASS();

    TEST("alloc 1 byte, 写入, free");
    p = (unsigned char *)tlibc_malloc(1);
    if (!p) { FAIL("alloc returned NULL"); return; }
    p[0] = 'X';
    if (p[0] != 'X') { FAIL("data corruption"); return; }
    tlibc_free(p);
    PASS();
}

/* ========= 4. 多次分配 + 释放 ========= */
static void test_multi_alloc_free(void)
{
    void *ptrs[32];

    TEST("连续 alloc 32 次 + 全部 free");
    for (int i = 0; i < 32; i++) {
        ptrs[i] = tlibc_malloc((i + 1) * 64);
        if (!ptrs[i]) { FAIL("alloc returned NULL"); return; }
    }
    for (int i = 0; i < 32; i++)
        tlibc_free(ptrs[i]);
    PASS();

    TEST("交错 alloc/free");
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 16; i++)
            ptrs[i] = tlibc_malloc(128);
        for (int i = 0; i < 16; i += 2)
            tlibc_free(ptrs[i]);       /* 释放偶数位 */
        for (int i = 0; i < 16; i += 2)
            ptrs[i] = tlibc_malloc(128);
        for (int i = 0; i < 16; i++)
            tlibc_free(ptrs[i]);
    }
    PASS();
}

/* ========= 5. 大块分配 ========= */
static void test_large_alloc(void)
{
    void *ptrs[N_LARGE];

    TEST("连续 alloc 32 x 1MiB + free");
    for (int i = 0; i < N_LARGE; i++) {
        ptrs[i] = tlibc_malloc(LARGE_SZ);
        if (!ptrs[i]) { FAIL("large alloc returned NULL"); return; }
        /* 只写首尾页验证映射 */
        *(volatile char *)ptrs[i] = 0xAB;
        *(volatile char *)((char *)ptrs[i] + LARGE_SZ - 1) = 0xBA;
    }
    for (int i = 0; i < N_LARGE; i++)
        tlibc_free(ptrs[i]);
    PASS();
}

/* ========= 6. 循环复用（压力） ========= */
static void test_loop_reuse(void)
{
    void *p;

    TEST("alloc/free 循环 500 次");
    for (int i = 0; i < 500; i++) {
        p = tlibc_malloc(1024);
        if (!p) { FAIL("alloc returned NULL"); return; }
        tlibc_free(p);
    }
    PASS();

    TEST("alloc 递增大小 + free 100 轮");
    for (int i = 1; i <= 100; i++) {
        p = tlibc_malloc(i * 64);
        if (!p) { FAIL("alloc returned NULL"); return; }
        tlibc_free(p);
    }
    PASS();
}

/* ========= 7. malloc (mempool 版) + tlibc_free 混用 ========= */
static void test_malloc_mixed(void)
{
    void *p;

    TEST("malloc(128) + tlibc_free");
    p = malloc(128);
    if (!p || p == MAP_FAILED) { FAIL("malloc returned NULL/FAILED"); return; }
    tlibc_free(p);
    PASS();

    TEST("malloc + tlibc_free 交替 50 次");
    for (int i = 0; i < 50; i++) {
        p = malloc(256);
        if (!p || p == MAP_FAILED) { FAIL("malloc returned NULL"); return; }
        tlibc_free(p);
    }
    PASS();
}

/* ========= 8. 非对齐大小 ========= */
static void test_odd_sizes(void)
{
    void *p;

    TEST("alloc 3 byte + free");
    p = tlibc_malloc(3);
    if (!p) { FAIL("alloc returned NULL"); return; }
    tlibc_free(p);
    PASS();

    TEST("alloc 511 byte + free");
    p = tlibc_malloc(511);
    if (!p) { FAIL("alloc returned NULL"); return; }
    tlibc_free(p);
    PASS();

    TEST("alloc 4097 byte + free");
    p = tlibc_malloc(4097);
    if (!p) { FAIL("alloc returned NULL"); return; }
    tlibc_free(p);
    PASS();
}

/* ========= 9. 释放后写入应崩溃（悬空指针检测：只测可预见行为） ========= */
/*
 * 注意：释放后访问已回收内存是 UAF，行为未定义。
 * 这里只验证 tlibc_free 本身执行完毕未崩溃，不验证后续访问。
 */

int main(void)
{
    __printf("tlibc_free test suite\n"
             "=====================\n");

    test_basic_alloc_free();
    test_safe_free();
    test_write_then_free();
    test_multi_alloc_free();
    test_large_alloc();
    test_loop_reuse();
    test_malloc_mixed();
    test_odd_sizes();

    __printf("=====================\n"
             "%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
