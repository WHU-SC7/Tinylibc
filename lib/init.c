#include "core.h"
#include "pthread.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "init.h"

void *pre_alloc_stack; //没有考虑多线程对全局变量的竞争
int remain_thread_stack_num;

int main(int argc, char *argv[]);
int tlibc_init(int argc, char *argv[])
{
    //预先分配1000个线程的栈
    pre_alloc_stack = __mmap(0, PRE_ALLOC_SIZE*THREAD_STACK_SIZE, PROT_READ|PROT_WRITE, 
                 MAP_PRIVATE|MAP_ANON, -1, 0);
    __printf("使用栈%l\n", (long)pre_alloc_stack);
    if(pre_alloc_stack == (void *)-12)
    {
        __printf("预分配线程栈失败!\n"); //mmap限制
        return -1;
    }
    remain_thread_stack_num = 1000;

    return main(argc, argv);
}

