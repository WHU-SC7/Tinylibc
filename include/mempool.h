#ifndef __MEMPOOL_H
#define __MEMPOOL_H

#include "tlibc_types.h"  /* pid_t */

int tlibc_mem_pool_init();
void *malloc(unsigned long size);
int tlibc_clean_thread_mem(pid_t tid);
int tlibc_scan_global_mem_list();
void tlibc_debug_print_mem_list();
int tlibc_register_thread_stack(pid_t tid, long stack_base, long stack_size);
int tlibc_find_or_alloc_thread_idx(pid_t tid);
int tlibc_mempool_mark_exit(pid_t tid);
void *tlibc_mempool_worker(void* arg);
void tlibc_mempool_stop_worker();
int tlibc_mempool_enable_auto_reclaim();

enum tlibc_thread_state_t{
    TLIBC_TS_UNKNOWN,
    TLIBC_TS_MAIN,
    TLIBC_TS_ALIVE,
    TLIBC_TS_EXIT,
};

#endif