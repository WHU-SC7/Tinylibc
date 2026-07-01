#ifndef __SYS_EPOLL_H
#define __SYS_EPOLL_H

#include "tlibc_types.h"  /* size_t, uint32_t, uint64_t */

/* ── 标志 ── */
#define EPOLL_CLOEXEC  02000000   /* close-on-exec */

/* ── 控制操作 ── */
#define EPOLL_CTL_ADD  1   /* 添加目标 fd 到 epoll 实例 */
#define EPOLL_CTL_DEL  2   /* 从 epoll 实例删除目标 fd */
#define EPOLL_CTL_MOD  3   /* 修改目标 fd 的关联事件 */

/* ── 事件 ── */
#define EPOLLIN     0x001
#define EPOLLPRI    0x002
#define EPOLLOUT    0x004
#define EPOLLERR    0x008
#define EPOLLHUP    0x010
#define EPOLLRDNORM 0x040
#define EPOLLRDBAND 0x080
#define EPOLLWRNORM 0x100
#define EPOLLWRBAND 0x200
#define EPOLLMSG    0x400
#define EPOLLRDHUP  0x2000
#define EPOLLONESHOT 0x40000000
#define EPOLLET      0x80000000   /* 边缘触发 */

/* ── 结构 ── */
union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
};

struct epoll_event {
    uint32_t          events;   /* 事件位掩码 */
    union epoll_data  data;     /* 用户数据 */
};

/* epoll_data_t 类型别名 */
typedef union epoll_data epoll_data_t;

/* ── 函数声明 ── */
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

/* epoll_create (老版本，只接受 size 参数，内核忽略) */
int epoll_create(int size);

/* epoll_pwait — 带信号掩码的 epoll_wait */
int epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                int timeout, const sigset_t *sigmask);

#endif /* __SYS_EPOLL_H */
