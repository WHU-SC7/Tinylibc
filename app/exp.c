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
#endif

// 对比测试Tinylibc相比于Glibc的性能，在pthread上
int main(int argc, char *argv[])
{
    struct timespec ts;
    struct timespec ts1;
    timespec_get(&ts, TIME_UTC);
    double t = ts.tv_sec+ ts.tv_nsec / 1e9;
    printf("start time: %f\n", t);

    timespec_get(&ts1, TIME_UTC);
    double t1 = ts1.tv_sec+ ts1.tv_nsec / 1e9;//end time
    double elapsed = t1 - t;
    printf("elapsed time: %f\n", elapsed);

    return 0;
}