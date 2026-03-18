#include "core.h"
#include "errno.h"
#include "pthread.h"

//现在只有pthread_create和pthread_join

static int start(void *p)
{
    struct start_args {
        void *(*start_func)(void *);
        void *start_arg;
    } *args = p;
    
    args->start_func(args->start_arg);
    
    /* 线程退出 */
    __exit(0);
    return 0;
}

/*
    sys_clone           int (*fn)(void *)   线程入口函数，pthread_create默认设为库自带的start
                        void *stack         线程栈
                        int flags           选项，如CLONE_FILES共享fd
                        void *arg           传给线程的参数
                        pid_t *parent_tid   存父线程tid
                        void *tls           线程本地存储，自定义的struct pthread
                        pid_t *child_tid    大概存子线程tid

    pthread_create      pthread_t *restrict res                 指针，是struct pthread的地址，空间在线程自己的栈上
                        const pthread_attr_t *restrict attrp,   删减后未使用
                        void *(*entry)(void *),                 入口函数
                        void *restrict arg)                     传给线程的参数
*/
int __pthread_create(pthread_t *restrict res, 
                     const pthread_attr_t *restrict attrp, 
                     void *(*entry)(void *), 
                     void *restrict arg)
{
    size_t size;//, guard;
    struct pthread *new;
    unsigned char *map = 0, *stack = 0;
    unsigned flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND
        | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS
        | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

    /* 简化：使用默认栈大小 */
    //guard = 0;  // 简化：不使用保护页
    size = 200* 4096 * 1024;  // 默认 4MB 栈
    
    /* 分配内存 */
    map = __mmap(0, size, PROT_READ|PROT_WRITE, 
                 MAP_PRIVATE|MAP_ANON, -1, 0);
    if (map == MAP_FAILED) return EAGAIN;
    
    stack = map + size;
    
    /* 初始化线程结构 */
    new = (struct pthread *)(stack - sizeof(struct pthread));
    __memset(new, 0, sizeof(struct pthread));
    
    new->map_base = map;
    new->map_size = size;
    new->tid = -1;
    new->self = new;
    
    /* 设置参数 */
    stack -= sizeof(void *) * 2;  // 为参数留空间
    struct { void *(*func)(void *); void *arg; } *args = 
        (void *)stack;
    args->func = entry; //入口函数
    args->arg = arg;
    
    /* 创建线程 */
    int ret = __clone(start, stack, flags, args, &new->tid, 
                      new, 0);
    
    if (ret < 0) {
        __munmap(map, size);
        return EAGAIN;
    }
    //因为类型定义与musl不同，加上类型转换。让res指向线程栈底部的struct pthread
    *res = (pthread_t)new;
    return 0;
}

//下面的定义并没有使用
#define FUTEX_WAIT_BITSET	9
#define FUTEX_CLOCK_REALTIME 256
//pthread->detach_state字段的取值
enum {
	DT_EXITED = 0,
	DT_EXITING,
	DT_JOINABLE,
	DT_DETACHED,
};

//简单的pthread_join，等待指定的线程退出.完全不保证与musl或glibc通用！！！完全不保证！
int __pthread_join(pthread_t t, void **res) //res不使用
{
    // __futex(tid_addr, FUTEX_WAIT_BITSET|FUTEX_CLOCK_REALTIME, tid, NULL, (void *)0 ,0); //glibc的pthread_join的futex调用类似这样
    struct pthread *thread = (struct pthread *)t;
    //不管线程刚创建时detach_state的值，似乎变化没有规律？？
    //检查线程是否推出按照glibc的做法。musl的做法难懂
    while(thread->tid != 0) //简单的等待。clone时CLONE_CHILD_CLEARTID标志，让内核在子线程退出时把tid设为0
    {
        // __printf("从tls获取child thread tid: %d, detach_status: %d\n", thread->tid, thread->detach_state);
        tlibc_msleep(100); //如果一直检查浪费CPU。之后考虑futex更好，为了简单先用睡眠替代
    }
    // __printf("从tls获取child thread tid: %d, detach_status: %d\n", thread->tid, thread->detach_state);
    // __printf("准备回收线程栈\n");
	if (thread->map_base) __munmap(thread->map_base, thread->map_size); //回收按照glibc的做法
    return 0;
}