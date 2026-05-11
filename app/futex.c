#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"

int thread_info = 0; //验证子线程能主进程的变量值，虽然没有用原子变量和操作，但是目前用睡眠1秒保证没有竞争

#define FUTEX_WAIT         0
#define FUTEX_WAKE         1
#define FUTEX_WAIT_BITSET	9
#define FUTEX_WAKE_BITSET  10
#define FUTEX_CLOCK_REALTIME 256

void* thread_func(void* arg) {
    tlibc_msleep(3000);
    thread_info = 1;
    //唤醒时val表示唤醒线程的数量
    futex((uint32_t *)&thread_info, FUTEX_WAKE, ~0, NULL, NULL, 0);
    __printf("Hello from thread %d\n", __gettid());
    return (void *)0;
}

int main(int argc, char *argv[])
{
    __printf("pthread test\n");
    //期望中，两个线程会打印不同的线程号
    pthread_t thread;
    pthread_create(&thread, NULL, thread_func, NULL);
    struct pthread *pthread = (struct pthread *)thread; //通过pthread_create的res获取子线程信息
    struct pthread *t = pthread;
    __printf("detach_state值: %d, 偏移: %d\n", t->detach_state, ((char *)&t->detach_state)-(char *)t);
    __printf("从tls获取child thread tid: %d\n", pthread->tid);
    uint32_t expected = thread_info;//假设thread_info没有被改变
    int ret = futex((uint32_t *)&thread_info, FUTEX_WAIT_BITSET|FUTEX_CLOCK_REALTIME, expected, NULL, (void *)0 , 1);
    printf("futex结束, 返回值%d\n", ret);
    pthread_join(thread, NULL);
    // tlibc_msleep(1000); //thread_func打印可能会交错，因为gettid调用时可能线程被调度
    pthread_t thread1;
    pthread_create(&thread1, NULL, thread_func, NULL);
    tlibc_msleep(1000);
    if(thread_info==1)
        __printf("验证！子线程改变了thread_info的值为: %d\n", thread_info);
    return 0;
}