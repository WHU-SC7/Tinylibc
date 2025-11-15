#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

int __internal_test(int argc, char *argv[])
{
    // unsigned long time_second;
    // unsigned long time_second_1;
    // __time(&time_second);
    // __printf("第一次获取的时间是: %d\n", time_second);
    // //睡眠一会
    // tlibc_msleep(1000);
    // __time(&time_second_1);
    // __printf("第二次获取的时间是: %d\n", time_second_1);

    struct timespec tp;
    struct timespec tp1;
    __clock_gettime(CLOCK_REALTIME, &tp);
    __printf("开始时间: %d+%d微秒\n", tp.st_atime_sec, tp.st_atime_nsec/1000);

    //do something
    /*
    for(long i=0; i<21000000000; i++) ;// 总共21G次循环，63G条指令。CPU频率4.2G,用时10.6s. 每条指令用时0.7周期。每纳秒执行6条指令
    for(long i=0; i<10000000; i++) __getpid(); //10M次系统调用,2.3秒。 每次调用耗时0.23微秒. 230纳秒，1400指令 //服务器是0.9秒，每次0.09微秒，90纳秒
    for(long i=0; i<100000; i++) __getpid(); //使用strace 10k次, 0.46秒,每次46微秒
    for(long i=0; i<10000000; i++) __yield(); //10M次yield,6.4秒。 每次yield耗时0.64微秒 640纳秒，3800指令
    */
    // for(long i=0; i<10000000; i++) __yield(); //10M次yield,6.4秒。 每次yield耗时0.64微秒 640纳秒，3800指令 //服务器是2.6秒,每次0.26微秒
    // int fd = __creat("tmp001", 0644);
    // if(fd<0)
    //     panic("fd < 0");
    // for(long i=0; i<1000000; i++) //1M次lseek, 0.26秒， 每次耗时0.26微秒 //服务器是0.11秒， 每次0.11微秒
    //     __lseek(fd, i, SEEK_SET);
    // for(long i=0; i<10000000; i++) __getpid();

    //fork测试，结论: GB级别的进程fork耗时是100ms级别，耗时与内存大小线性相关
    int pid;
    // #define SIZE (long)4294967296//0.5秒 4G
    // #define SIZE (long)2294967296//0.003
    // #define SIZE (long)1024 //0.0004
    #define SIZE (long)8589934592 //0.002 . 0.11秒,写入
    char *ptr = tlibc_malloc(SIZE);//分配大内存
    for(long i=0; i<SIZE/4096; i++)//写入一些内容，确保进程真实占有物理内存
    {
        //如果8G内存每字节写入，耗时约15秒. 写入4k字节耗时4微秒
        //如果每页只写入1字节，耗时约4秒。这是缺页异常的开销。8G是2M页。1秒是500k次，一次开销是2微秒
        ptr[i*4096] = 1;
    }
     __clock_gettime(CLOCK_REALTIME, &tp);
    __printf("fork前的时间: %d+%d微秒\n", tp.st_atime_sec, tp.st_atime_nsec/1000);
    pid = __fork();
    if(pid ==0)//fork出子进程时，内核为父进程占有的物理页的引用计数+1，这是耗时的主要来源
    {
        ptr[0] = 2;
        __exit(0);
    }
    
    

    __clock_gettime(CLOCK_REALTIME, &tp1);
    long sec = tp1.st_atime_sec-tp.st_atime_sec;
    long nsec;
    if(tp1.st_atime_nsec < tp.st_atime_nsec)
    {
        sec--;
        nsec = 1000000000 + tp1.st_atime_nsec - tp.st_atime_nsec;
    }
    else
        nsec = tp1.st_atime_nsec - tp.st_atime_nsec;
    __printf("结束时间: %l+%l微秒, 用时: %l+%l微秒\n", tp1.st_atime_sec, tp1.st_atime_nsec/1000, sec, nsec/1000);
    

    return 0;
}