#ifndef __PTHREAD_H
#define __PTHREAD_H

#include "tlibc_types.h"    /* size_t */
#include "tlibc.h"          /* _NSIG, sigset_t */

#ifdef __cplusplus
extern "C" {
#endif

#define NULL ((void *)0)

/* ========= 线程栈大小 ========= */
#define THREAD_STACK_SIZE (1024 * 1024)   /* 1 MiB — 比原来 4 MiB 更轻量 */

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

/* ========= clone / mmap 常量 ========= */
#define CSIGNAL       0x000000ff
#define CLONE_VM      0x00000100
#define CLONE_FS      0x00000200
#define CLONE_FILES   0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_PIDFD   0x00001000
#define CLONE_PTRACE  0x00002000
#define CLONE_VFORK   0x00004000
#define CLONE_PARENT  0x00008000
#define CLONE_THREAD  0x00010000
#define CLONE_NEWNS   0x00020000
#define CLONE_SYSVSEM 0x00040000
#define CLONE_SETTLS  0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_DETACHED       0x00400000
#define CLONE_UNTRACED       0x00800000
#define CLONE_CHILD_SETTID   0x01000000
#define CLONE_NEWCGROUP      0x02000000
#define CLONE_NEWUTS         0x04000000
#define CLONE_NEWIPC         0x08000000
#define CLONE_NEWUSER        0x10000000
#define CLONE_NEWPID         0x20000000
#define CLONE_NEWNET         0x40000000
#define CLONE_IO             0x80000000

#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0
#define PROT_GROWSDOWN  0x01000000
#define PROT_GROWSUP    0x02000000

#define MAP_SHARED       0x01
#define MAP_PRIVATE      0x02
#define MAP_SHARED_VALIDATE 0x03
#define MAP_TYPE         0x0f
#define MAP_ANONYMOUS    0x20
#define MAP_ANON         MAP_ANONYMOUS

#define MAP_FAILED ((void *) -1)

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

#ifdef __cplusplus
}
#endif

#endif /* __PTHREAD_H */
