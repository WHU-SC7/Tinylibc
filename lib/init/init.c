#include "core.h"
#include "pthread.h"
#include "tlibc_print.h"
#include "mempool.h"
#include "tlibc_everything.h"
#include "syscall.h"
#include "syscall_num.h"

pid_t main_tid;

/* 主线程的 struct pthread（作为 TLS 区域） */
static struct pthread main_pthread;

int main(int argc, char *argv[]);

static void setup_main_thread_tls(void)
{
    /* 初始化主线程的 struct pthread */
    __memset(&main_pthread, 0, sizeof(struct pthread));
    main_pthread.self        = &main_pthread;
    main_pthread.dtv         = NULL;
    main_pthread.canary      = 0;
    main_pthread.tid         = __gettid();   /* 主线程的 tid */
    main_pthread.errno_val   = 0;
    main_pthread.detach_state = PTHREAD_CREATE_JOINABLE;
    main_pthread.map_base    = NULL;          /* 主线程栈不由我们管理 */
    main_pthread.map_size    = 0;
    main_pthread.result      = NULL;

    /* 用 arch_prctl 设置 fs_base = &main_pthread */
    unsigned long fs_base = (unsigned long)&main_pthread;
    long ret = syscall(SYS_arch_prctl, 0x1002 /* ARCH_SET_FS */, fs_base);
    if (ret != 0) {
        __printf("Warning: arch_prctl(ARCH_SET_FS) failed, ret=%ld\n", ret);
    }
}

int tlibc_init(int argc, char *argv[], char *envp[])
{
    global_envp = envp;
    main_tid = __gettid();

    /* 为主线程设置 TLS（fs_base），使 pthread_self() 可工作 */
    setup_main_thread_tls();

    /* 初始化内存池（提供 malloc） */
    tlibc_mem_pool_init();

    int ret = main(argc, argv);

    if (__gettid() == main_tid)
        __exit_group(ret);

    return ret;
}
