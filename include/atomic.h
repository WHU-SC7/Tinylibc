#ifndef __ATOMIC_H
#define __ATOMIC_H

#include "tlibc_types.h"

// Atomic fetch-and-add (x86_64 lock xadd)

static inline uint64_t atomic_fetch_add_u64(volatile uint64_t *ptr, uint64_t val) {
    uint64_t result;
    __asm__ volatile(
        "lock xaddq %0, %1"
        : "=r" (result), "+m" (*ptr)
        : "0" (val)
        : "memory", "cc"
    );
    return result;
}

static inline uint32_t atomic_fetch_add_u32(volatile uint32_t *ptr, uint32_t val) {
    uint32_t result;
    __asm__ volatile(
        "lock xaddl %0, %1"
        : "=r" (result), "+m" (*ptr)
        : "0" (val)
        : "memory", "cc"
    );
    return result;
}

// Atomic compare-and-swap (x86_64 lock cmpxchg)
static inline uint64_t atomic_compare_exchange_u64(volatile uint64_t *ptr, 
                                                   uint64_t expected, 
                                                   uint64_t desired) {
    uint64_t result = expected;
    __asm__ volatile(
        "lock cmpxchgq %2, %1"
        : "=a" (result), "+m" (*ptr)
        : "r" (desired), "0" (result)
        : "memory", "cc"
    );
    return result;
}

static inline uint32_t atomic_compare_exchange_u32(volatile uint32_t *ptr,
                                                   uint32_t expected,
                                                   uint32_t desired) {
    uint32_t result = expected;
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a" (result), "+m" (*ptr)
        : "r" (desired), "0" (result)
        : "memory", "cc"
    );
    return result;
}
#endif