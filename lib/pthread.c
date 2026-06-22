#include "core.h"
#include "errno.h"
#include "pthread.h"
#include "tlibc_print.h"
#include "init.h"
#include "mempool.h"

/* Internal thread entry: runs the user function then calls pthread_exit */
static int start(void *p)
{
    struct start_args {
        void *(*start_func)(void *);
        void *start_arg;
    } *args = p;

    void *ret = args->start_func(args->start_arg);

    pthread_exit(ret);
    return 0;
}

/*
   sys_clone args:  fn(stack, flags, arg, parent_tid, tls, child_tid)
   pthread_create:  res(ptr to struct pthread on child's stack), attrp(unused),
                    entry(user func), arg(user arg)
*/
int pthread_create(pthread_t *restrict res,
                     const pthread_attr_t *restrict attrp,
                     void *(*entry)(void *),
                     void *restrict arg)
{
    size_t size;
    struct pthread *new;
    unsigned char *map = 0, *stack = 0;
    unsigned flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND
        | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS
        | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

    size = THREAD_STACK_SIZE;

    // Pre-allocated stacks: batch mmap to avoid per-thread mmap overhead
    if(remain_thread_stack_num==0){
        pre_alloc_stack = __mmap(0, PRE_ALLOC_SIZE*THREAD_STACK_SIZE, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANON, -1, 0);
        if(pre_alloc_stack == (void *)-1)
        {
            __printf("Failed to pre-allocate thread stacks!\n");
            return EAGAIN;
        }
        remain_thread_stack_num = PRE_ALLOC_SIZE;
    }
    map = (unsigned char *)pre_alloc_stack;
    pre_alloc_stack = ((char *)pre_alloc_stack + THREAD_STACK_SIZE);
    remain_thread_stack_num --;

    stack = map + size;

    // Initialize pthread struct at the bottom of the stack
    new = (struct pthread *)(stack - sizeof(struct pthread));
    __memset(new, 0, sizeof(struct pthread));

    new->map_base = map;
    new->map_size = size;
    new->tid = -1;
    new->self = new;

    // Place args below the pthread struct
    stack -= sizeof(void *) * 2;
    struct { void *(*func)(void *); void *arg; } *args =
        (void *)stack;
    args->func = entry;
    args->arg = arg;

    int ret = __clone(start, stack, flags, args, &new->tid,
                      new, &new->tid);

    if (ret < 0) {
        pre_alloc_stack = ((char *)pre_alloc_stack - THREAD_STACK_SIZE);
        remain_thread_stack_num ++;
        __printf("clone failed!\n");
        return EAGAIN;
    }
    tlibc_register_thread_stack(new->tid, (long)map, size);
    *res = (pthread_t)new;
    return 0;
}

// Unused futex/detach-state definitions kept for reference
#define FUTEX_WAIT_BITSET   9
#define FUTEX_CLOCK_REALTIME 256
enum {
    DT_EXITED = 0,
    DT_EXITING,
    DT_JOINABLE,
    DT_DETACHED,
};

/*
   pthread_join is intentionally a no-op in this implementation.
   Thread resource reclamation is handled asynchronously by a background
   worker thread (see mempool.c). The worker scans for exited threads and
   reclaims their stacks and heap allocations.
   NOTE: retval is ignored since we never synchronously wait.
*/
int pthread_join(pthread_t t, void **res)
{
    return 0;
}

/* Marks the calling thread as exited so the mempool worker can reclaim its resources */
void pthread_exit(void *retval)
{
    tlibc_mempool_mark_exit(__gettid());
    __exit(0);
}