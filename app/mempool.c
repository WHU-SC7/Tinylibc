#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"

void* thread_func(void* arg) {
    // struct pthread *pthread = (struct pthread *)arg;
    // __printf("从struct pthread, %l获取child thread tid: %d\n", pthread, pthread->tid);
    tlibc_msleep(1000);
    // __printf("Hello from thread %d\n", __gettid());
    return (void *)0;
}

int main(int argc, char *argv[])
{
    __printf("mempool test\n");
    pthread_t thread;
    __pthread_create(&thread, NULL, thread_func, NULL);
    struct pthread *pthread = (struct pthread *)thread;
    // tlibc_msleep(2000); //决定join时子线程是否已经退出
    __printf("从struct pthread, %l获取child thread tid: %d\n", pthread ,pthread->tid);
    __pthread_join(thread, NULL);
    
    return 0;
}