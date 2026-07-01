#ifndef __PTHREAD_H
#define __PTHREAD_H

#include "tlibc_types.h"    /* size_t */

/* ========= 线程栈大小 ========= */
#define THREAD_STACK_SIZE (1024 * 1024)   /* 1 MiB */

/* ========= pthread 属性 ========= */
#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1

#define __SIZEOF_PTHREAD_ATTR_T 56
typedef union {
    char __size[__SIZEOF_PTHREAD_ATTR_T];
    long int __align;
} pthread_attr_t;

/* ========= 线程标识符 ========= */
typedef struct pthread *pthread_t;

/* ========= struct pthread ========= */
struct pthread {
    /* ── TLS 区域 — fs_base 指向此处 ── */
    struct pthread *self;       /* [ 0] 指向自身 */
    unsigned long   *dtv;       /* [ 8] TLS 向量 */
    unsigned long    canary;    /* [16] 占位保留 */

    /* ── 身份 ── */
    int tid;                    /* [24] 内核通过 CLONE_CHILD_CLEARTID 清 0 */
    int errno_val;              /*      per-thread errno */
    volatile int detach_state;  /*      PTHREAD_CREATE_JOINABLE 或 _DETACHED */

    /* ── 栈资源 — 供 pthread_join munmap ── */
    void  *map_base;
    size_t map_size;

    /* ── 返回值 — 供 pthread_join 读取 ── */
    void *result;
};

/*
 * 子线程启动参数（放在栈上，传给 start 包装函数）
 * 不在公开头文件里暴露实现细节，仅在 pthread.c 中定义。
 * 这里保留仅供类型检查。
 */

/* ========= 线程 API ========= */
int pthread_create(pthread_t *restrict res,
                   const pthread_attr_t *restrict attrp,
                   void *(*entry)(void *),
                   void *restrict arg);
int pthread_join(pthread_t t, void **res);
void pthread_exit(void *retval);
int pthread_detach(pthread_t t);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);

#endif /* __PTHREAD_H */
