#ifndef __MEMPOOL_H
#define __MEMPOOL_H

#include "tlibc.h"

int mem_pool_init();
void *malloc(unsigned long size);
int clean_with_tid(pid_t tid);
int scan_and_clean_global_mem_list();
void debug_print_global_mem_list();
int register_thread_stack(pid_t tid, long stack_base, long stack_size);
int find_or_alloc_thread_idx(pid_t tid);

enum Thread_state{
    UNKNOWN,
    MAIN_THREAD, //初始的主线程
    ALIVE,
    EXIT,
};

#endif