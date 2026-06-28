#include "core.h"
#include "atomic.h"
#include "pthread.h"
#include "errno.h"
#include "string.h"

// 自定义自旋锁实现
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock) {
    lock->lock = 0;
}

static inline void spinlock_lock(spinlock_t *lock) {
    while (1) {
        // 如果lock为0，则尝试设置为1
        uint32_t expected = 0;
        uint32_t desired = 1;
        if (atomic_compare_exchange_u32(&lock->lock, expected, desired) == expected) {
            break;
        }
        // 让出CPU
        __yield();
    }
}

static inline void spinlock_unlock(spinlock_t *lock) {
    lock->lock = 0;
}

#define QUEUE_SIZE 65536

// 队列结构
typedef struct {
    spinlock_t spinlock;
    volatile uint32_t num;        // 使用volatile配合原子操作
    int head;
    int tail;
    int value[QUEUE_SIZE];
} Queue;

long thread_num = 128; //工作线程数
long task_time = 1000000; //要执行的总次数

// 全局计数器
volatile uint32_t task_limit = 0;
volatile uint32_t produced = 0;
volatile uint32_t consumed = 0;

void init_queue(Queue *queue) {
    spinlock_init(&queue->spinlock);
    queue->num = 0;
    queue->head = 0;
    queue->tail = 0;
    __memset(queue->value, 0, sizeof(queue->value));
}

void destroy_queue(Queue *queue) {
    // 自旋锁无需特殊销毁
}

// 使用自旋锁的入队操作
int enqueue(Queue *queue, int value) {
    spinlock_lock(&queue->spinlock);
    
    uint32_t num = queue->num;
    if (num == QUEUE_SIZE) {
        spinlock_unlock(&queue->spinlock);
        return -1;  // 队列满
    }
    
    queue->value[queue->head] = value;
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    atomic_fetch_add_u32(&queue->num, 1);
    
    spinlock_unlock(&queue->spinlock);
    return 0;
}

// 原子减操作辅助函数
static inline uint32_t atomic_fetch_sub_u32(volatile uint32_t *ptr, uint32_t val) {
    return atomic_fetch_add_u32(ptr, -val);
}

// 使用自旋锁的出队操作
int dequeue(Queue *queue, int *success) {
    spinlock_lock(&queue->spinlock);
    
    uint32_t num = queue->num;
    if (num == 0) {
        spinlock_unlock(&queue->spinlock);
        *success = 0;
        return 0;
    }
    
    int ret = queue->value[queue->tail];
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    atomic_fetch_sub_u32(&queue->num, 1);
    
    spinlock_unlock(&queue->spinlock);
    *success = 1;
    return ret;
}

// 生产者线程。生产和消费
void* producer_task(void* arg) {
    Queue* queue = (Queue*)arg;
    int success;
    
    while (produced < task_time) {
        if (enqueue(queue, 2) == 0) {
            atomic_fetch_add_u32(&produced, 1);
        }
        // 主动让出CPU，减少争用
        if (produced % 100 == 0) {
            __yield();
        }
        
        dequeue(queue, &success);
        if (success) {
            atomic_fetch_add_u32(&consumed, 1);
        }
    }
    
    return NULL;
}

// 消费者线程
void* consumer_task(void* arg) {
    Queue* queue = (Queue*)arg;
    int success;
    
    while (consumed < task_time) {
        dequeue(queue, &success);
        if (success) {
            atomic_fetch_add_u32(&consumed, 1);
        } else {
            // 队列空时让出CPU
            __yield();
        }
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if(argc == 2)//一个参数
    {
        thread_num = tlibc_strtoul(argv[1]);
    }
    if(argc == 3)//两个参数
    {
        thread_num = tlibc_strtoul(argv[1]);
        task_time = tlibc_strtoul(argv[2]);
    }
    __printf("线程数: %d, 执行次数: %d\n", thread_num, task_time);
    struct timespec tp, tp1;
    __clock_gettime(CLOCK_REALTIME, &tp);
    __printf("开始时间: %ld秒 %ld纳秒\n", tp.tv_sec, tp.tv_nsec);

    // 初始化队列
    Queue *queue = (Queue*)tlibc_malloc(sizeof(Queue));
    if (!queue) {
        __printf("内存分配失败\n");
        return -1;
    }
    init_queue(queue);
    __printf("队列初始化完成\n");

    // 创建线程
    pthread_t producers[thread_num];
    // pthread_t consumers[thread_num];
    int ret;
    
    // 创建生产者线程
    for (int i = 0; i < thread_num; i++) {
        ret = pthread_create(&producers[i], NULL, producer_task, queue);
        if (ret != 0) {
            __printf("创建生产者线程失败: %d\n", ret);
            return -1;
        }
    }
    
    // 创建消费者线程（如果取消注释）
    // for (int i = 0; i < thread_num; i++) {
    //     ret = pthread_create(&consumers[i], NULL, consumer_task, queue);
    //     if (ret != 0) {
    //         __printf("创建消费者线程失败: %d\n", ret);
    //         return -1;
    //     }
    // }
    
    // 等待所有线程完成
    for (int i = 0; i < thread_num; i++) {
        pthread_join(producers[i], NULL);
        // pthread_join(consumers[i], NULL);
    }
    
    // 验证结果
    __printf("生产数量: %d\n", produced);
    __printf("消费数量: %d\n", consumed);
    __printf("队列剩余元素: %d\n", queue->num);
    
    __clock_gettime(CLOCK_REALTIME, &tp1);
    long sec = tp1.tv_sec - tp.tv_sec;
    long nsec = tp1.tv_nsec - tp.tv_nsec;
    
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    
    __printf("结束时间: %ld秒 %ld纳秒\n", tp1.tv_sec, tp1.tv_nsec);
    __printf("总用时: %ld秒 %ld微秒\n", sec, nsec/1000);
    // long throughout = consumed * 1000 / (sec * 1000 + nsec/1000000);
    // __printf("每秒执行次数: %ldK\n", throughout);
    
    // 清理资源
    destroy_queue(queue);

    return 0;
}