#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"

#define THREAD_MEM 100*1024*1024
#define THREAD_NUM 100

//这两个函数表明，栈和mmap分配的内存在使用时才占用物理内存。不使用则不占用，释放也非常快
void* thread_func(void* arg) { 
    char *ptr = (char *)tlibc_malloc(THREAD_MEM);
    if(ptr == (char *)-1)
    {
        __printf("malloc失败!\n");
        __exit(0);
    }
    for(int i=0;i<THREAD_MEM/4096;i++)
    {
        ptr[0] = 45;
        ptr += 4096;
    }
    while(1) __yield();
    return (void *)0;
}
#define STACK_MEM 200*1024*1020
void* void_thread_func(void* arg) {
    __printf("Hello from thread %d\n", __gettid());
    char stackvec[STACK_MEM];//200MB
    char *ptr = stackvec;
    for(int i=0;i<STACK_MEM/4096;i++)
    {
        ptr[0] = 94;
        ptr += 4096;
    }

    __printf("已使用stack:%d\n", stackvec);
    while(1) __yield();
    return (void *)0;
}

int main(int argc, char *argv[])
{
    __printf("memtest\n");
    pthread_t thread;
    for(int i=0;i<THREAD_NUM; i++)
    {
        // tlibc_msleep(100);
        pthread_create(&thread, NULL, void_thread_func, NULL);
    }
    for(int i=0;i<5;i++)
    {
        __printf("主线程等待中\n");
        tlibc_msleep(1000);
    }
    __printf("开始清理\n");
    //主线程如果调用exit,不管子线程，子线程会处于无人管理的状态
    //调用exit_group以结束所有线程，回收所有资源
    __exit_group(0);
    return 0;
}