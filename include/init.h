#ifndef __INIT_H
#define __INIT_H

extern void * volatile pre_alloc_stack;
extern volatile int remain_thread_stack_num;
#define PRE_ALLOC_SIZE (long)100//每次预先分配的数量

#endif