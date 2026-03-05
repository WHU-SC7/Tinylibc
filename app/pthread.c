#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

#define NULL ((void *)0)

struct start_args {
	void *(*start_func)(void *);
	void *start_arg;
	volatile int control;
	unsigned long sig_mask[_NSIG/8/sizeof(long)];
};

typedef unsigned long pthread_t;

#define __SIZEOF_PTHREAD_ATTR_T 56
union pthread_attr_t
{
  char __size[__SIZEOF_PTHREAD_ATTR_T];
  long int __align;
};
#ifndef __have_pthread_attr_t
typedef union pthread_attr_t pthread_attr_t;
# define __have_pthread_attr_t 1
#endif
# define CSIGNAL       0x000000ff /* Signal mask to be sent at exit.  */
# define CLONE_VM      0x00000100 /* Set if VM shared between processes.  */
# define CLONE_FS      0x00000200 /* Set if fs info shared between processes.  */
# define CLONE_FILES   0x00000400 /* Set if open files shared between processes.  */
# define CLONE_SIGHAND 0x00000800 /* Set if signal handlers shared.  */
# define CLONE_PIDFD   0x00001000 /* Set if a pidfd should be placed
				     in parent.  */
# define CLONE_PTRACE  0x00002000 /* Set if tracing continues on the child.  */
# define CLONE_VFORK   0x00004000 /* Set if the parent wants the child to
				     wake it up on mm_release.  */
# define CLONE_PARENT  0x00008000 /* Set if we want to have the same
				     parent as the cloner.  */
# define CLONE_THREAD  0x00010000 /* Set to add to same thread group.  */
# define CLONE_NEWNS   0x00020000 /* Set to create new namespace.  */
# define CLONE_SYSVSEM 0x00040000 /* Set to shared SVID SEM_UNDO semantics.  */
# define CLONE_SETTLS  0x00080000 /* Set TLS info.  */
# define CLONE_PARENT_SETTID 0x00100000 /* Store TID in userlevel buffer
					   before MM copy.  */
# define CLONE_CHILD_CLEARTID 0x00200000 /* Register exit futex and memory
					    location to clear.  */
# define CLONE_DETACHED 0x00400000 /* Create clone detached.  */
# define CLONE_UNTRACED 0x00800000 /* Set if the tracing process can't
				      force CLONE_PTRACE on this clone.  */
# define CLONE_CHILD_SETTID 0x01000000 /* Store TID in userlevel buffer in
					  the child.  */
# define CLONE_NEWCGROUP    0x02000000	/* New cgroup namespace.  */
# define CLONE_NEWUTS	0x04000000	/* New utsname group.  */
# define CLONE_NEWIPC	0x08000000	/* New ipcs.  */
# define CLONE_NEWUSER	0x10000000	/* New user namespace.  */
# define CLONE_NEWPID	0x20000000	/* New pid namespace.  */
# define CLONE_NEWNET	0x40000000	/* New network namespace.  */
# define CLONE_IO	0x80000000	/* Clone I/O context.  */

#define PROT_READ	0x1		/* Page can be read.  */
#define PROT_WRITE	0x2		/* Page can be written.  */
#define PROT_EXEC	0x4		/* Page can be executed.  */
#define PROT_NONE	0x0		/* Page can not be accessed.  */
#define PROT_GROWSDOWN	0x01000000	/* Extend change to start of
					   growsdown vma (mprotect only).  */
#define PROT_GROWSUP	0x02000000	/* Extend change to start of
					   growsup vma (mprotect only).  */
/* Sharing types (must choose one and only one of these).  */
#define MAP_SHARED	0x01		/* Share changes.  */
#define MAP_PRIVATE	0x02		/* Changes are private.  */
# define MAP_SHARED_VALIDATE	0x03	/* Share changes and validate
					   extension flags.  */
# define MAP_TYPE	0x0f		/* Mask for type of mapping.  */
#  define MAP_ANONYMOUS	0x20		/* Don't use a file.  */
# define MAP_ANON	MAP_ANONYMOUS

#define MAP_FAILED ((void *) -1)

// #define pthread __pthread

struct pthread {
// 	/* Part 1 -- these fields may be external or
// 	 * internal (accessed via asm) ABI. Do not change. */
	struct pthread *self;
// #ifndef TLS_ABOVE_TP
// 	uintptr_t *dtv;
// #endif
// 	struct pthread *prev, *next; /* non-ABI */
// 	uintptr_t sysinfo;
// #ifndef TLS_ABOVE_TP
// #ifdef CANARY_PAD
// 	uintptr_t canary_pad;
// #endif
// 	uintptr_t canary;
// #endif

	/* Part 2 -- implementation details, non-ABI. */
	int tid;
	int errno_val;
	volatile int detach_state;
	volatile int cancel;
	volatile unsigned char canceldisable, cancelasync;
	unsigned char tsd_used:1;
	unsigned char dlerror_flag:1;
	unsigned char *map_base;
	size_t map_size;
	void *stack;
	size_t stack_size;
	size_t guard_size;
	void *result;
	// struct __ptcb *cancelbuf;
	void **tsd;
	struct {
		volatile void *volatile head;
		long off;
		volatile void *volatile pending;
	} robust_list;
	int h_errno_val;
	volatile int timer_id;
	// locale_t locale;
	volatile int killlock[1];
	char *dlerror_buf;
	void *stdio_locks;

// 	/* Part 3 -- the positions of these fields relative to
// 	 * the end of the structure is external and internal ABI. */
// #ifdef TLS_ABOVE_TP
// 	uintptr_t canary;
// 	uintptr_t *dtv;
// #endif
};

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
    size = 4096 * 1024;  // 默认 4MB 栈
    
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
    args->func = entry;
    args->arg = arg;
    
    /* 创建线程 */
    int ret = __clone(start, stack, flags, args, &new->tid, 
                      new, 0);
    
    if (ret < 0) {
        __munmap(map, size);
        return EAGAIN;
    }
    // *res = new;
    return 0;
}

int thread_info = 0; //验证子线程能主进程的变量值，虽然没有用原子变量和操作，但是目前用睡眠1秒保证没有竞争

void* thread_func(void* arg) {
    thread_info = 1;
    __printf("Hello from thread %d\n", __gettid());
    return (void *)0;
}

void pthread(int argc, char *argv[])
{
    __printf("pthread test\n");
    //期望中，两个线程会打印不同的线程号
    pthread_t thread;
    __pthread_create(&thread, NULL, thread_func, NULL);
    // tlibc_msleep(1000); //thread_func打印可能会交错，因为gettid调用时可能线程被调度
    pthread_t thread1;
    __pthread_create(&thread1, NULL, thread_func, NULL);
    tlibc_msleep(1000);
    if(thread_info==1)
        __printf("验证！子线程改变了thread_info的值为: %d\n", thread_info);
    __exit(0);
}