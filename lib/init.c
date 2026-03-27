#include "core.h"
#include "pthread.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "init.h"
#include "mempool.h"

void *pre_alloc_stack; //没有考虑多线程对全局变量的竞争
int remain_thread_stack_num;

int main(int argc, char *argv[]);
int tlibc_init(int argc, char *argv[])
{
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
    mem_pool_init();

    int ret = main(argc, argv);
    inform_work_thread_to_exit();//先让工作线程退出
    return ret;
}

