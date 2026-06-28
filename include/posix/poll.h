#ifndef __POLL_H
#define __POLL_H

#include "tlibc_types.h"  /* size_t */

/* ── 事件标志 ── */
#define POLLIN      0x001   /* 有数据可读 */
#define POLLPRI     0x002   /* 紧急数据可读 */
#define POLLOUT     0x004   /* 可写 */
#define POLLERR     0x008   /* 发生错误（仅返回，不设置） */
#define POLLHUP     0x010   /* 挂起（仅返回，不设置） */
#define POLLNVAL    0x020   /* 无效请求：fd 未打开（仅返回，不设置） */
#define POLLRDNORM  0x040   /* 普通数据可读（同 POLLIN） */
#define POLLRDBAND  0x080   /* 优先级带数据可读 */
#define POLLWRNORM  0x100   /* 普通数据可写（同 POLLOUT） */
#define POLLWRBAND  0x200   /* 优先级带数据可写 */
#define POLLMSG     0x400   /* 消息可用（SYS V 消息队列） */
#define POLLRDHUP   0x2000  /* 流式半关闭或 shutdown（可设置） */

/* ── 类型 ── */
typedef unsigned long nfds_t;

/* ── 结构 ── */
struct pollfd {
    int    fd;       /* 要监视的文件描述符 */
    short  events;   /* 请求的事件（位掩码） */
    short  revents;  /* 返回的事件（位掩码，内核填写） */
};

/* ── 函数声明 ── */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds,
          const struct timespec *timeout_ts, const sigset_t *sigmask);

#endif /* __POLL_H */
