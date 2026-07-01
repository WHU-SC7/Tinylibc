/* SPDX-License-Identifier: MIT
 *
 * poll.c — I/O 多路复用（poll / select / epoll）
 *
 * poll / ppoll     → __poll / __ppoll（__NR_poll / __NR_ppoll）
 * select / pselect → __pselect6（__NR_pselect6）
 * epoll_*          → __epoll_*（__NR_epoll_create1 / _ctl / _wait / _pwait）
 *
 * 约定：返回 -errno 而非 -1/errno（项目规范）。
 * 分组：三者都是 I/O 多路复用，合在同一文件减少源文件数量。
 */

#include "core.h"

/* ================================================================== */
/*  poll / ppoll                                                        */
/* ================================================================== */

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return __poll(fds, nfds, timeout);
}

int ppoll(struct pollfd *fds, nfds_t nfds,
          const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    return __ppoll(fds, nfds, timeout_ts, sigmask);
}

/* ================================================================== */
/*  select / pselect                                                    */
/* ================================================================== */

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    struct timespec ts, *tsp = NULL;

    if (timeout) {
        ts.tv_sec  = timeout->tv_sec;
        ts.tv_nsec = timeout->tv_usec * 1000L;
        tsp = &ts;
    }

    return __pselect6(nfds, readfds, writefds, exceptfds, tsp, NULL);
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask)
{
    return __pselect6(nfds, readfds, writefds, exceptfds, timeout, sigmask);
}

/* ================================================================== */
/*  epoll_create1 / epoll_ctl / epoll_wait / epoll_pwait                */
/* ================================================================== */

int epoll_create1(int flags)
{
    return __epoll_create1(flags);
}

int epoll_create(int size)
{
    (void)size;
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return __epoll_ctl(epfd, op, fd, event);
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return __epoll_wait(epfd, events, maxevents, timeout);
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                int timeout, const sigset_t *sigmask)
{
    return __epoll_pwait(epfd, events, maxevents, timeout, sigmask);
}
