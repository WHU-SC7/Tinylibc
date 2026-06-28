#include "core.h"
#include "tlibc_print.h"
#include "errno.h"
#include "pthread.h"

#define THREAD_MEM (1024*1024)    /* 1MB 每线程（之前 100MB 太激进） */
#define THREAD_NUM 4              /* 4 线程（之前 100 个太高） */

//这两个函数表明，栈和mmap分配的内存在使用时才占用物理内存。不使用则不占用，释放也非常快
void* thread_func(void* arg) {
    char *ptr = (char *)tlibc_malloc(THREAD_MEM);
    if(ptr == (char *)-1)
    {
        __printf("malloc失败!\n");
        return (void *)-1;
    }
    for(int i=0;i<THREAD_MEM/4096;i++)
    {
        ptr[0] = 45;
        ptr += 4096;
    }
    __printf("线程 %d: malloc 内存写入完成\n", __gettid());
    return (void *)0;
}
#define STACK_MEM (64*1024)        /* 64KB 栈 VLA（之前 ≈200MB 会撑爆线程栈） */
void* void_thread_func(void* arg) {
    __printf("Hello from thread %d\n", __gettid());
    char stackvec[STACK_MEM];
    char *ptr = stackvec;
    for(int i=0;i<STACK_MEM/4096;i++)
    {
        ptr[0] = 94;
        ptr += 4096;
    }

    __printf("线程 %d: 栈内存写入完成,stack=%p\n", __gettid(), (void *)stackvec);
    return (void *)0;
}

int main(int argc, char *argv[])
{
    __printf("memtest\n");
    pthread_t threads[THREAD_NUM];
    for(int i=0;i<THREAD_NUM; i++)
    {
        pthread_create(&threads[i], NULL, void_thread_func, NULL);
        printf("创建线程 %d\n", i);
    }
    for(int i=0;i<THREAD_NUM;i++)
    {
        pthread_join(threads[i], NULL);
        printf("线程 %d 已回收\n", i);
    }
    __printf("memtest 完成\n");
    return 0;
}