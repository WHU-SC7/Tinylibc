#include "core.h"
#include "errno.h"
#include "pthread.h"
#include "pthread_arch.h"
#include "string.h"    /* __memset */
#include "sched.h"     /* CLONE_* 标志 */
#include "tlibc_print.h"

/* 子线程启动参数（放在栈上） */
struct start_args {
    void *(*func)(void *);
    void *arg;
};

/* Internal thread entry: runs the user function then calls pthread_exit */
static int start(void *p)
{
    struct start_args *args = p;
    void *ret = args->func(args->arg);
    pthread_exit(ret);
    return 0;   /* never reached */
}

int pthread_create(pthread_t *restrict res,
                   const pthread_attr_t *restrict attrp,
                   void *(*entry)(void *),
                   void *restrict arg)
{
    (void)attrp;    /* TODO: handle PTHREAD_CREATE_DETACHED from attr */

    /* 1. mmap 线程栈 */
    unsigned char *map = __mmap(NULL, THREAD_STACK_SIZE,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANON, -1, 0);
    if (map == MAP_FAILED) {
        __printf("pthread_create: mmap stack failed\n");
        return EAGAIN;
    }

    /* 2. 栈顶（栈向下增长） */
    unsigned char *stack_top = map + THREAD_STACK_SIZE;

    /* 3. struct pthread 放在栈底附近（同时也是 TLS 区域） */
    struct pthread *new = (struct pthread *)(stack_top - sizeof(struct pthread));
    __memset(new, 0, sizeof(struct pthread));

    new->self        = new;
    new->dtv         = NULL;
    new->canary      = 0;
    new->tid         = -1;         /* CLONE_PARENT_SETTID 会写入实际值 */
    new->detach_state = PTHREAD_CREATE_JOINABLE;
    new->map_base    = map;
    new->map_size    = THREAD_STACK_SIZE;
    new->result      = NULL;

    /* 4. start_args 放在 struct pthread 下方 */
    unsigned char *sp = (unsigned char *)new - sizeof(struct start_args);
    struct start_args *sargs = (struct start_args *)sp;
    sargs->func = entry;
    sargs->arg  = arg;

    /* 5. clone 子线程 */
    unsigned flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND
                   | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS
                   | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

    int ret = __clone(start, sp, flags, sargs,
                      &new->tid,       /* parent_tid  — 内核写 tid */
                      new,             /* tls         — fs_base = new */
                      &new->tid);      /* child_tid   — 退出时内核清 0 */

    if (ret < 0) {
        __munmap(map, THREAD_STACK_SIZE);
        return EAGAIN;
    }

    *res = (pthread_t)new;
    return 0;
}

int pthread_join(pthread_t t, void **res)
{
    struct pthread *p = (struct pthread *)t;

    /* 不允许 join 已分离的线程 */
    if (p->detach_state == PTHREAD_CREATE_DETACHED)
        return EINVAL;

    /* 等待内核清除 tid（CLONE_CHILD_CLEARTID） */
    while (p->tid != 0) {
        __futex((unsigned int *)&p->tid, 0 /* FUTEX_WAIT */,
                p->tid, NULL, NULL, 0);
    }

    if (res)
        *res = p->result;

    /* 回收线程栈 + struct pthread */
    __munmap(p->map_base, p->map_size);
    return 0;
}

void pthread_exit(void *retval)
{
    struct pthread *self = __tlibc_thread_self();
    self->result = retval;

    /* 退出线程 — 内核自动清除 tid 并唤醒 futex waiter */
    __exit(0);
}

int pthread_detach(pthread_t t)
{
    struct pthread *p = (struct pthread *)t;

    /* 标记为已分离，后续 pthread_join 会返回 EINVAL。
     *
     * 注意：这里不检查 tid==0 主动 munmap——因为此时另一个线程可能正在
     * pthread_join 中读取 p 的字段（尽管已经 detach 了就不该 join）。
     * 更关键的是，如果我们在 detach 时释放了栈，然后调用者又误调了
     * pthread_join，就会 UAF。
     *
     * detached 线程的栈回收留待后续实现（自回收或 manager 线程）。
     */
    p->detach_state = PTHREAD_CREATE_DETACHED;
    return 0;
}

pthread_t pthread_self(void)
{
    return (pthread_t)__tlibc_thread_self();
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}
