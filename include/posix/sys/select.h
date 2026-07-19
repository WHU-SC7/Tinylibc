#ifndef __SYS_SELECT_H
#define __SYS_SELECT_H

#include "tlibc_types.h"  /* size_t, time_t */

/* suseconds_t — 微秒精度有符号整数 */
typedef long suseconds_t;

/* timeval 由 time.h 提供（POSIX 要求 sys/select.h 使其可见） */
#include "time.h"

/* ── fd_set — 位图实现（FD_SETSIZE=1024） ── */
#define FD_SETSIZE  1024

typedef struct {
    unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

/* ── 宏 ── */
#define FD_ZERO(set)                                         \
    do {                                                     \
        int __i;                                             \
        for (__i = 0;                                        \
             __i < (int)(sizeof((set)->fds_bits) /           \
                         sizeof(*(set)->fds_bits));           \
             __i++)                                          \
            (set)->fds_bits[__i] = 0UL;                      \
    } while (0)

#define FD_SET(fd, set)                                      \
    ((set)->fds_bits[(fd) / (8 * (int)sizeof(unsigned long))] \
        |= (1UL << ((fd) % (8 * (int)sizeof(unsigned long)))))

#define FD_CLR(fd, set)                                      \
    ((set)->fds_bits[(fd) / (8 * (int)sizeof(unsigned long))] \
        &= ~(1UL << ((fd) % (8 * (int)sizeof(unsigned long)))))

#define FD_ISSET(fd, set)                                    \
    ((set)->fds_bits[(fd) / (8 * (int)sizeof(unsigned long))] \
        & (1UL << ((fd) % (8 * (int)sizeof(unsigned long)))))

/* ── 函数声明 ── */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);

/* 注：pselect6 的声明在 core.h（__pselect6），这里不重复，
 * 因为 POSIX pselect() 由 lib/select.c 提供。 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask);

#endif /* __SYS_SELECT_H */
