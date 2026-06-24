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
        void **result_ptr;   /* points to struct pthread.result */
    } *args = p;

    void *ret = args->start_func(args->start_arg);

    /* Store return value so pthread_join can retrieve it */
    if (args->result_ptr) {
        *args->result_ptr = ret;
    }

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
    new->result = NULL;

    // Place args below the pthread struct (3 pointers: func, arg, result_ptr)
    stack -= sizeof(void *) * 3;
    struct {
        void *(*func)(void *);
        void *arg;
        void **result_ptr;
    } *args = (void *)stack;
    args->func = entry;
    args->arg = arg;
    args->result_ptr = &new->result;

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

/* Futex operations */
#define FUTEX_WAIT          0
#define FUTEX_WAKE          1

int pthread_join(pthread_t t, void **res)
{
    struct pthread *p = (struct pthread *)t;

    /* Wait until the target thread exits.  The kernel clears ->tid and
       wakes futex waiters when the thread exits via CLONE_CHILD_CLEARTID. */
    while (p->tid != 0) {
        __futex((unsigned int *)&p->tid, FUTEX_WAIT, p->tid, NULL, NULL, 0);
    }

    /* Return the thread's result if requested */
    if (res) {
        *res = p->result;
    }

    return 0;
}

/* Marks the calling thread as exited so the mempool worker can reclaim its resources */
void pthread_exit(void *retval)
{
    tlibc_mempool_mark_exit(__gettid());
    __exit(0);
}