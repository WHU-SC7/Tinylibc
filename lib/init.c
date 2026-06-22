#include "core.h"
#include "pthread.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "init.h"
#include "mempool.h"
#include "tlibc_everything.h"

void *pre_alloc_stack; //没有考虑多线程对全局变量的竞争
int remain_thread_stack_num;
pid_t main_tid;

int main(int argc, char *argv[]);
int tlibc_init(int argc, char *argv[], char *envp[])
{
    global_envp = envp;
    tlibc_print_all_env_vars(envp);
    main_tid = __gettid();
    //预先分配1000个线程的栈
    pre_alloc_stack = __mmap(0, PRE_ALLOC_SIZE*THREAD_STACK_SIZE, PROT_READ|PROT_WRITE, 
                 MAP_PRIVATE|MAP_ANON, -1, 0);
    if(pre_alloc_stack == (void *)-12)
    {
        __printf("预分配线程栈失败!\n"); //mmap限制
        return -1;
    }
    remain_thread_stack_num = PRE_ALLOC_SIZE;

    //初始化mempool
    tlibc_mem_pool_init();

    int ret = main(argc, argv);
    if(__gettid() == main_tid)
        __exit_group(0);
    tlibc_mempool_stop_worker();
    return ret;
}

