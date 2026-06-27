/*
 * pthread 单元测试
 *
 * 覆盖：
 *   基本 create/join / 返回值传递 / 多线程 / self/equal / detach 语义
 *
 * 构建 & 运行：
 *   tmake -b thread
 *   build/output/thread
 *   # 或直接在 output 目录：  build/app/test/thread
 */

#include "core.h"
#include "pthread.h"
#include "errno.h"

/* 测试计数 */
static int passed, failed;
#define TEST(name)   do { __printf("  " name " ... "); } while(0)
#define PASS()       do { __printf("OK\n"); passed++; } while(0)
#define FAIL(reason) do { __printf("FAIL: %s\n", reason); failed++; } while(0)

/* 线程函数的返回指针，验证返回值传递 */
static void *return_self(void *arg)
{
    return arg;
}

static void *return_42(void *arg)
{
    (void)arg;
    return (void *)42;
}

static void *return_null(void *arg)
{
    (void)arg;
    return NULL;
}

/* 写入共享变量，验证线程确实独立运行 */
static volatile int shared_flag;
static void *set_flag(void *arg)
{
    shared_flag = *(int *)arg;
    return NULL;
}

/* ========= 1. 基本 create / join 与返回值传递 ========= */
static void test_create_join_return(void)
{
    pthread_t t;
    void *ret;

    TEST("pthread_create + join, return (void*)42");
    shared_flag = 0;
    if (pthread_create(&t, NULL, return_42, NULL) != 0) {
        FAIL("create failed");
        return;
    }
    if (pthread_join(t, &ret) != 0) {
        FAIL("join failed");
        return;
    }
    if ((long)ret != 42) {
        FAIL("expected 42");
        return;
    }
    PASS();

    TEST("pthread_create + join, return NULL");
    if (pthread_create(&t, NULL, return_null, NULL) != 0) {
        FAIL("create failed");
        return;
    }
    if (pthread_join(t, &ret) != 0) {
        FAIL("join failed");
        return;
    }
    if (ret != NULL) {
        FAIL("expected NULL");
        return;
    }
    PASS();

    TEST("pthread_create + join, return self pointer");
    if (pthread_create(&t, NULL, return_self, (void *)0xabcdef) != 0) {
        FAIL("create failed");
        return;
    }
    if (pthread_join(t, &ret) != 0) {
        FAIL("join failed");
        return;
    }
    if ((long)ret != 0xabcdef) {
        FAIL("expected 0xabcdef");
        return;
    }
    PASS();
}

/* ========= 2. 多线程并发 ========= */
static void test_multi_thread(void)
{
    pthread_t t1, t2, t3;
    void *r1, *r2, *r3;

    TEST("three threads running concurrently");
    if (pthread_create(&t1, NULL, return_42, NULL) != 0) { FAIL("create t1"); return; }
    if (pthread_create(&t2, NULL, return_self, (void *)99) != 0) { FAIL("create t2"); return; }
    if (pthread_create(&t3, NULL, return_null, NULL) != 0) { FAIL("create t3"); return; }

    if (pthread_join(t1, &r1) != 0) { FAIL("join t1"); return; }
    if (pthread_join(t2, &r2) != 0) { FAIL("join t2"); return; }
    if (pthread_join(t3, &r3) != 0) { FAIL("join t3"); return; }

    if ((long)r1 != 42)         { FAIL("t1 expected 42"); return; }
    if ((long)r2 != 99)         { FAIL("t2 expected 99"); return; }
    if (r3 != NULL)             { FAIL("t3 expected NULL"); return; }
    PASS();
}

/* ========= 3. pthread_self + pthread_equal ========= */
static void *self_checker(void *arg)
{
    pthread_t *expected = (pthread_t *)arg;
    if (pthread_equal(pthread_self(), *expected))
        return (void *)1;       /* match */
    return (void *)0;           /* no match */
}

static void test_self_equal(void)
{
    pthread_t t;

    TEST("pthread_self returns non-zero");
    if (pthread_self() == (pthread_t)0) {
        FAIL("pthread_self returned 0");
        return;
    }
    PASS();

    TEST("pthread_equal(main, main)");
    if (!pthread_equal(pthread_self(), pthread_self())) {
        FAIL("main != main");
        return;
    }
    PASS();

    TEST("pthread_equal(thread, its own self)");
    if (pthread_create(&t, NULL, self_checker, &t) != 0) {
        FAIL("create failed");
        return;
    }
    void *ret;
    if (pthread_join(t, &ret) != 0) {
        FAIL("join failed");
        return;
    }
    if ((long)ret != 1) {
        FAIL("thread did not match its own self");
        return;
    }
    PASS();
}

/* ========= 4. detached 线程 join 返回 EINVAL ========= */
static void *noop(void *arg)
{
    (void)arg;
    return (void *)123;
}

static void test_detach_join_fails(void)
{
    pthread_t t;
    void *ret;

    TEST("pthread_detach + pthread_join returns EINVAL");
    if (pthread_create(&t, NULL, noop, NULL) != 0) {
        FAIL("create failed");
        return;
    }
    if (pthread_detach(t) != 0) {
        FAIL("detach failed");
        return;
    }
    /* detached 线程退出需要一点时间; 等待后 join 应失败 */
    tlibc_msleep(100);
    int rc = pthread_join(t, &ret);
    if (rc != EINVAL) {
        __printf("expected EINVAL(%d), got %d\n", EINVAL, rc);
        FAIL("join of detached thread did not return EINVAL");
        return;
    }
    PASS();

    TEST("pthread_create with attr PTHREAD_CREATE_DETACHED + join fails");
    {
        pthread_attr_t attr;
        /* 最简单的 attr 设置方式：直接设 detachstate */
        /* 这里依赖实现（Phase 5），先跳过如果 API 不支持 */
        int built_with_attr = 0;
        /* 用 __get_ptattr 之类的？ 等 Phase 5 完善后放开 */
        (void)attr;
        if (!built_with_attr) {
            __printf("SKIP (attr not wired yet)\n");
            /* 不算失败 */
            return;
        }
    }
}

/* ========= 5. 大量线程：验证 mmap / munmap 循环 ========= */
#define N_MANY 20
static void test_many_threads(void)
{
    pthread_t t[N_MANY];
    void *ret;

    __printf("  create + join %d threads sequentially ... ", N_MANY);
    for (int i = 0; i < N_MANY; i++) {
        if (pthread_create(&t[i], NULL, return_42, NULL) != 0) {
            __printf("FAIL at i=%d\n", i);
            FAIL("create failed");
            return;
        }
    }
    for (int i = 0; i < N_MANY; i++) {
        if (pthread_join(t[i], &ret) != 0) {
            __printf("FAIL at i=%d\n", i);
            FAIL("join failed");
            return;
        }
        if ((long)ret != 42) {
            __printf("FAIL at i=%d (got %ld)\n", i, (long)ret);
            FAIL("wrong return value");
            return;
        }
    }
    PASS();
}

/* ========= 6. 线程修改共享内存 ========= */
static void test_shared_memory(void)
{
    int val = 7;

    TEST("child writes to shared variable via pointer");
    shared_flag = 0;
    pthread_t t;
    if (pthread_create(&t, NULL, set_flag, &val) != 0) {
        FAIL("create failed");
        return;
    }
    if (pthread_join(t, NULL) != 0) {
        FAIL("join failed");
        return;
    }
    if (shared_flag != 7) {
        FAIL("expected shared_flag=7");
        return;
    }
    PASS();
}

int main(void)
{
    __printf("pthread test suite\n"
             "==================\n");

    test_create_join_return();
    test_multi_thread();
    test_self_equal();
    test_detach_join_fails();
    test_many_threads();
    test_shared_memory();

    __printf("==================\n"
             "%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
