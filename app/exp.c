#if X86_64_TLIBC == 1
//使用Tinylibc
#include "core.h"
#include "pthread.h"
#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "mempool.h"
#else
//使用Glibc
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#endif

#define THREAD_NUM_PER_CIRCLE 1000

void* thread_func(void* arg) {
    for(int i=0;i<1000000;i++); //400微秒
    return (void *)0;
}

// 对比测试Tinylibc相比于Glibc的性能，在pthread上
int main(int argc, char *argv[])
{
    for(int i=0;i<8;i++)
    {
    struct timespec ts;
    struct timespec ts1;
    timespec_get(&ts, TIME_UTC);
    double t = ts.tv_sec+ ts.tv_nsec / 1e9;
    printf("start time: %f\n", t);

    pthread_t thread;
    for(int i=0;i<THREAD_NUM_PER_CIRCLE;i++)
    {
        pthread_create(&thread, NULL, thread_func, NULL);
        pthread_join(thread, NULL);
    }

    timespec_get(&ts1, TIME_UTC);
    double t1 = ts1.tv_sec+ ts1.tv_nsec / 1e9;//end time
    double elapsed = t1 - t;
    printf("elapsed time: %f, 每秒线程数: %f\n", elapsed, THREAD_NUM_PER_CIRCLE / elapsed);
    usleep(1000000);
}
    return 0;
}