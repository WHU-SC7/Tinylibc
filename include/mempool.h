#ifndef __MEMPOOL_H
#define __MEMPOOL_H

#include "tlibc.h"

int mem_pool_init();
void *malloc(unsigned long size);
int clean_with_tid(pid_t tid);
int scan_and_clean_global_mem_list();
void debug_print_global_mem_list();

#endif