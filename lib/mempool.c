#include "core.h"
#include "pthread.h"
#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "mempool.h"

#define MAX_THREAD_OF_MEMPOOL 256
struct global_mem_list{
    long thread_id_idx[MAX_THREAD_OF_MEMPOOL]; 
    struct thread_mem_list* thread_mem_list[MAX_THREAD_OF_MEMPOOL];
};
struct thread_mem_list{
    pid_t tid;
    int chunk_num;
    struct mem_chunk *first;
    long stack_base;
    long stack_size;
    enum Thread_state state;
};
struct mem_chunk{
    struct mem_chunk *next;
    long base; //映射起始
    long size; //映射长度
    long flag; //保留符号位
};

struct global_mem_list *global_mem_list;
char whether_have_init = 0;//禁止未初始化时使用
int work_thread_exit = 0;

int mem_pool_init()
{
    if(whether_have_init != 1){
        global_mem_list = tlibc_malloc(sizeof(struct global_mem_list));
        int thread_idx = find_or_alloc_thread_idx(__gettid()); //记录主线程
        global_mem_list->thread_mem_list[thread_idx]->state = MAIN_THREAD;
        whether_have_init = 1;
        //创建工作线程，定期回收已经退出的线程资源
        pthread_t thread;
        pthread_create(&thread, NULL, mempool_work_thread_func, NULL);
        return 0;
    }
    else return 0;
}

int find_or_alloc_thread_idx(pid_t tid)
{
    int thread_idx = -1; //请求malloc的线程对应的idex
    for(int i=0; i<MAX_THREAD_OF_MEMPOOL; i++)
    {
        if(global_mem_list->thread_id_idx[i]==0){//选择这个空位,并分配一个struct thread_mem_list
            thread_idx = i;
            struct thread_mem_list *t_mem_list = tlibc_malloc(sizeof(struct thread_mem_list));
            t_mem_list->tid = tid;//记录tid
            global_mem_list->thread_id_idx[i] = tid;
            global_mem_list->thread_mem_list[i] = t_mem_list;
            break;
        }
        if(tid == global_mem_list->thread_id_idx[i])
        {
            thread_idx = i;
            break;
        }
    }
    if(thread_idx == -1)
        return -1;
    else
        return thread_idx;
}

int register_thread_stack(pid_t tid, long stack_base, long stack_size)
{
    int thread_idx = find_or_alloc_thread_idx(tid);
    if(thread_idx == -1){
        __printf("ERROR!列表已满\n");
        return -1;
    }
    struct thread_mem_list *t_mem_list = global_mem_list->thread_mem_list[thread_idx];
    t_mem_list->stack_base = stack_base;
    t_mem_list->stack_size = stack_size;
    t_mem_list->state = ALIVE;
    return thread_idx;
}

//标识一个线程的退出，pthread_exit调用
int mempool_mark_thread_exit(pid_t tid)
{
    int thread_idx = find_or_alloc_thread_idx(tid);
    if(thread_idx == -1){
        __printf("ERROR!没有找到指定线程\n");
        return -1;
    }
    struct thread_mem_list *t_mem_list = global_mem_list->thread_mem_list[thread_idx];
    if(t_mem_list->state == MAIN_THREAD)
        return -1;//禁止让主线程退出
    t_mem_list->state = EXIT;
    __printf("线程%d标记为EXIT\n", tid);
    return 0;
}


//扫描一遍，清理已经退出的线程
int mempool_clean_thread()
{
    for(int i=0; i<MAX_THREAD_OF_MEMPOOL; i++){
        if(global_mem_list->thread_id_idx[i] == 0)//扫描完了
            break;
        if(global_mem_list->thread_mem_list[i]->state == EXIT){
            __printf("回收线程%d的资源\n", global_mem_list->thread_id_idx[i]);
            struct thread_mem_list *t_mem_list = global_mem_list->thread_mem_list[i];
            struct mem_chunk *chunk = t_mem_list->first; 
            //回收线程栈
            int ret = __munmap((void *)t_mem_list->stack_base, t_mem_list->stack_size);
            if(ret==0)
                __printf("回收栈成功!base: %ld, size: %ld\n", t_mem_list->stack_base, t_mem_list->stack_size);
            else
                __printf("ERROR!回收栈失败\n");
            for(int i=0; i<t_mem_list->chunk_num; i++){ //回收所有映射和struct mem_chunk
                if(__munmap((void *)chunk->base, chunk->size) == -1){
                    __printf("ERROR! 回收内存失败, base: %ld, size: %ld\n", chunk->base, chunk->size);
                }
                __printf("成功回收内存, base: %ld, size: %ld\n", chunk->base, chunk->size);
                chunk = chunk->next;
                __munmap((void *)chunk, sizeof(struct mem_chunk));
            }
            //线程内存全部释放，回收struct thread_mem_list;
            __munmap((void *)t_mem_list, sizeof(struct thread_mem_list));
            //前移填空，保持有序
            for(int j= i; j<MAX_THREAD_OF_MEMPOOL; j++){
                global_mem_list->thread_id_idx[j] = global_mem_list->thread_id_idx[j+1];
                global_mem_list->thread_mem_list[j] = global_mem_list->thread_mem_list[j+1];
            }
            i--; //因为前移，要-1
        }
    }
    return 0;
}

//负责清理的线程
void *mempool_work_thread_func(void* arg)
{
    while(1){
        if(work_thread_exit == 1)
            __exit(0);
        tlibc_msleep(1000); //每次睡眠1秒
        mempool_clean_thread();
    }
}

void inform_work_thread_to_exit()
{
    work_thread_exit = 1;
}

void *malloc(unsigned long size)
{
    if(whether_have_init == 0)
    {
        __printf("malloc失败! 禁止未初始化时使用\n");
        return (void *)-2;
    }
    void *addr = __mmap(0, size, PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON, -1, 0);
    if(addr == MAP_FAILED)
        return MAP_FAILED;
    //记录
    pid_t tid = __gettid();
    int thread_idx = -1; //请求malloc的线程对应的idex
    for(int i=0; i<MAX_THREAD_OF_MEMPOOL; i++)
    {
        if(global_mem_list->thread_id_idx[i]==0){//选择这个空位,并分配一个struct thread_mem_list
            thread_idx = i;
            struct thread_mem_list *t_mem_list = tlibc_malloc(sizeof(struct thread_mem_list));
            t_mem_list->tid = tid;//记录tid
            global_mem_list->thread_id_idx[i] = tid;
            global_mem_list->thread_mem_list[i] = t_mem_list;
            break;
        }
        if(tid == global_mem_list->thread_id_idx[i])
        {
            thread_idx = i;
            break;
        }
    }
    if(thread_idx == -1)
    {
        __printf("ERROR! gloabl_mem_list已满\n");
    }
    //挂载一个mem_chunk,表示记录
    struct mem_chunk *mem_chunk = tlibc_malloc(sizeof(struct mem_chunk));
    mem_chunk->base = (long)addr;
    mem_chunk->size = size;
    struct thread_mem_list *t_mem_list = global_mem_list->thread_mem_list[thread_idx];
    if(t_mem_list->first == 0){
        t_mem_list->first = mem_chunk;
    }
    else{
        struct mem_chunk *m_ptr = t_mem_list->first;
        while(m_ptr->next != NULL)
            m_ptr = m_ptr->next;
        m_ptr->next = mem_chunk; //挂在链表末尾
    }
    t_mem_list->chunk_num ++;//计数+1
    __printf("在idx为%d为线程%d分配内存\n", thread_idx, tid);
    
    return addr;
}

int clean_with_tid(pid_t tid)
{
    int thread_idx = -1;
    for(int i=0; i<MAX_THREAD_OF_MEMPOOL; i++){
        if(global_mem_list->thread_id_idx[i] == 0)
            break;
        if(tid == global_mem_list->thread_id_idx[i])
            thread_idx = i;
    }
    if(thread_idx == -1){
        __printf("没有找到线程%d\n", tid);
        return -1;//没有找到要清理的线程内存
    }
    else{
        __printf("找到线程%d开始清理, idx: %d\n", tid, thread_idx);
        struct thread_mem_list *t_mem_list = global_mem_list->thread_mem_list[thread_idx];
        struct mem_chunk *chunk = t_mem_list->first; 
        for(int i=0; i<t_mem_list->chunk_num; i++){ //回收所有映射和struct mem_chunk
            if(__munmap((void *)chunk->base, chunk->size) == -1){
                __printf("ERROR! 回收内存失败, base: %ld, size: %ld\n", chunk->base, chunk->size);
            }
            chunk = chunk->next;
            __munmap((void *)chunk, sizeof(struct mem_chunk));
        }
        //线程内存全部释放，回收struct thread_mem_list;
        __munmap((void *)t_mem_list, sizeof(struct thread_mem_list));
        //前移填空，保持有序
        for(int i= thread_idx; i<MAX_THREAD_OF_MEMPOOL; i++){
            global_mem_list->thread_id_idx[i] = global_mem_list->thread_id_idx[i+1];
            global_mem_list->thread_mem_list[i] = global_mem_list->thread_mem_list[i+1];
        }
    }
    return 0;
}

//扫描/proc, 回收本进程中已经退出的线程
int scan_and_clean_global_mem_list()
{
    return 0;
}

void debug_print_global_mem_list()
{
    if(whether_have_init == 0)
    {
        __printf("debug_print_global_mem_list失败! 禁止未初始化时使用\n");
        return ;
    }
    __printf("\n--------------------------------------------------\n");
    int thread_count = 0;
    for(int i=0; i<MAX_THREAD_OF_MEMPOOL; i++){
        if(global_mem_list->thread_id_idx[i] == 0)
            break;
        thread_count++;
    }
    if(thread_count == 0){
        __printf("mempool没有记录!\n");
    }
    else{
        for(int i=0; i<thread_count; i++){
            struct thread_mem_list *t_mem_list = global_mem_list->thread_mem_list[i];
            struct mem_chunk *chunk = t_mem_list->first; 
            __printf("线程%d的状态: %d\n", t_mem_list->tid, t_mem_list->state);
            __printf("线程%d的栈base: %ld, size: %ld\n", t_mem_list->tid, t_mem_list->stack_base, t_mem_list->stack_size);
            __printf("线程%d的内存统计:\n", t_mem_list->tid);
            for(int j=0; j<t_mem_list->chunk_num; j++){
                __printf("\tchunk%d, map_base: %ld, map_size: %ld, flag: %ld\n", j, chunk->base, chunk->size, chunk->flag);
                chunk = chunk->next;
            }
        }
    }
    __printf("--------------------------------------------------\n\n");
}