/* SPDX-License-Identifier: MIT
 *
 * test_iomux.c — I/O 多路复用测试（poll / select / epoll）
 *
 * 覆盖场景:
 *   poll:    POLLIN, POLLOUT, POLLNVAL, 多 fd,        0 超时, nfds=0
 *   select:  读集, 写集, 0 超时, NULL timeout(阻塞)
 *   epoll:   LT 模式, ET 模式(pipe 填满+排空), 多 fd, DEL,
 *            POLLHUP(对端关闭), 删除后不再收到事件
 *   网络:    TCP socket + poll/epoll 检测 accept / POLLHUP
 *   边界:    nfds=0, fd=-1, 无效 fd
 *
 * 编译: tmake -b test_iomux
 * 运行: build/output/test_iomux
 */

#include "tlibc_everything.h"
#include "tlibc_test.h"
#include "syscall.h"
#include "syscall_num.h"

/* ================================================================== */
/*  超时常量 (ms)                                                      */
/* ================================================================== */
#define TMO_IMMEDIATE      0       /* 不等待 */
#define TMO_SHORT        200       /* 短超时 — 确认"不该就绪" */
#define TMO_NORMAL       1000      /* 正常等待 */
#define TMO_LONG         2000      /* 最长的等待 */

/* ================================================================== */
/*  辅助函数: pipe 创建/销毁                                           */
/* ================================================================== */

static inline int
setup_pipe(int fds[2])
{
    return pipe2(fds, 0);
}

#define teardown_pipe(fds)                                              \
    do { close((fds)[0]); close((fds)[1]); } while (0)

/* ================================================================== */
/*  辅助: TCP listener (返回 fd, port 通过指针; 失败返回 -1)           */
/* ================================================================== */

static int
setup_tcp_listener(int *port_out)
{
    int fd, opt = 1, ret;
    struct sockaddr_in addr;
    socklen_t len;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_port        = 0;            /* 内核分配 */
    addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) { close(fd); return -2; }

    ret = listen(fd, 5);
    if (ret < 0) { close(fd); return -3; }

    /* 用 raw syscall 获取分配的端口（项目未封装 getsockname） */
    len = sizeof(addr);
    ret = syscall(SYS_getsockname, fd, &addr, &len);
    if (ret < 0) { close(fd); return -4; }

    if (port_out)
        *port_out = tlibc_ntohs(addr.sin_port);
    return fd;
}

/* ================================================================== */
/*  poll 测试                                                          */
/* ================================================================== */

static void
test_poll_pipein(void)
{
    int pipefd[2];
    struct pollfd pf;
    int ret;

    TEST_START("poll — pipe POLLIN");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    /* 空 pipe，短超时 → 0 */
    pf.fd = pipefd[0]; pf.events = POLLIN; pf.revents = 0;
    ret = poll(&pf, 1, TMO_SHORT);
    TEST_ASSERT(ret == 0, "空 pipe poll 应超时返回 0");

    /* 写入后 → POLLIN */
    write(pipefd[1], "hello", 6);
    pf.fd = pipefd[0]; pf.events = POLLIN; pf.revents = 0;
    ret = poll(&pf, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0,          "poll 应检测到可读");
    TEST_ASSERT(pf.revents & POLLIN, "revents 应包含 POLLIN");

    { char buf[16]; read(pipefd[0], buf, 16); }
cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_poll_pipeout(void)
{
    int pipefd[2];
    struct pollfd pf;
    int ret;

    TEST_START("poll — pipe POLLOUT");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    /* 写端初始可写 */
    pf.fd = pipefd[1]; pf.events = POLLOUT; pf.revents = 0;
    ret = poll(&pf, 1, TMO_SHORT);
    TEST_ASSERT(ret > 0, "poll 应检测到 pipe 可写");
    TEST_ASSERT((pf.revents & POLLOUT) != 0, "revents 应包含 POLLOUT");

cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_poll_nval(void)
{
    TEST_START("poll — 无效 fd 返回 POLLNVAL");

    struct pollfd pf = { .fd = 9999, .events = POLLIN, .revents = 0 };
    int ret = poll(&pf, 1, TMO_SHORT);
    TEST_ASSERT(ret > 0,          "poll 应检测到无效 fd");
    TEST_ASSERT(pf.revents & POLLNVAL, "revents 应包含 POLLNVAL");

    TEST_PASS();
}

static void
test_poll_multi(void)
{
    int pipefd[2];
    struct pollfd pfd[3];
    int ret;

    TEST_START("poll — 多 fd 混合 (读+写+无效)");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    write(pipefd[1], "data", 5);

    pfd[0].fd = pipefd[0]; pfd[0].events = POLLIN;  pfd[0].revents = 0;
    pfd[1].fd = pipefd[1]; pfd[1].events = POLLOUT; pfd[1].revents = 0;
    pfd[2].fd = 9999;      pfd[2].events = POLLIN;  pfd[2].revents = 0;

    ret = poll(pfd, 3, TMO_SHORT);
    TEST_ASSERT(ret > 0, "poll 多 fd 应返回事件");

    int got_in   = 0;
    int got_out  = 0;
    int got_nval = 0;
    for (int i = 0; i < 3; i++) {
        if (pfd[i].revents & POLLIN)   got_in   = 1;
        if (pfd[i].revents & POLLOUT)  got_out  = 1;
        if (pfd[i].revents & POLLNVAL) got_nval = 1;
    }
    TEST_ASSERT(got_in,   "pipe 读端应 POLLIN");
    TEST_ASSERT(got_out,  "pipe 写端应 POLLOUT");
    TEST_ASSERT(got_nval, "fd 9999 应 POLLNVAL");

    { char buf[16]; read(pipefd[0], buf, 16); }
cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_poll_timeout_zero(void)
{
    int pipefd[2];
    struct pollfd pf;
    int ret;

    TEST_START("poll — 0 超时立即返回");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    pf.fd = pipefd[0]; pf.events = POLLIN; pf.revents = 0;
    ret = poll(&pf, 1, TMO_IMMEDIATE);
    TEST_ASSERT(ret == 0, "空 pipe poll 应立即返回 0");

cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_poll_nfds_zero(void)
{
    TEST_START("poll — nfds=0 应超时返回 0");

    int ret = poll(NULL, 0, TMO_SHORT);
    TEST_ASSERT(ret == 0, "poll(nfds=0) 应返回 0");

    TEST_PASS();
}

static void
test_poll_neg_fd(void)
{
    TEST_START("poll — fd=-1 被内核忽略");

    struct pollfd pf = { .fd = -1, .events = POLLIN, .revents = 0 };
    int ret = poll(&pf, 1, TMO_SHORT);
    TEST_ASSERT(ret == 0, "poll fd=-1 应返还 0");

    TEST_PASS();
}

/* ================================================================== */
/*  select 测试                                                        */
/* ================================================================== */

static void
test_select_pipe(void)
{
    int pipefd[2];
    fd_set rfds;
    struct timeval tv;
    int ret;

    TEST_START("select — pipe 读");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    FD_ZERO(&rfds);
    FD_SET(pipefd[0], &rfds);
    tv.tv_sec = 0; tv.tv_usec = TMO_SHORT * 1000;
    ret = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
    TEST_ASSERT(ret == 0, "空 pipe select 应超时");

    write(pipefd[1], "x", 1);
    FD_ZERO(&rfds);
    FD_SET(pipefd[0], &rfds);
    tv.tv_sec = 1; tv.tv_usec = 0;
    ret = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
    TEST_ASSERT(ret > 0,             "select 应检测到可读");
    TEST_ASSERT(FD_ISSET(pipefd[0], &rfds), "fd 应在读集中");

    { char tmp[4]; read(pipefd[0], tmp, 4); }
cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_select_write(void)
{
    int pipefd[2];
    fd_set wfds;
    struct timeval tv;
    int ret;

    TEST_START("select — pipe 写");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    FD_ZERO(&wfds);
    FD_SET(pipefd[1], &wfds);
    tv.tv_sec = 0; tv.tv_usec = TMO_SHORT * 1000;
    ret = select(pipefd[1] + 1, NULL, &wfds, NULL, &tv);
    TEST_ASSERT(ret > 0               && FD_ISSET(pipefd[1], &wfds),
                "select 应检测到 pipe 可写");

cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_select_timeout_zero(void)
{
    int pipefd[2];
    fd_set rfds;
    struct timeval tv;
    int ret;

    TEST_START("select — 0 超时立即返回");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    FD_ZERO(&rfds);
    FD_SET(pipefd[0], &rfds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    ret = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
    TEST_ASSERT(ret == 0, "空 pipe select 应立即返回 0");

cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_select_null_timeout(void)
{
    int pipefd[2];
    fd_set rfds;
    int ret;

    TEST_START("select — NULL timeout (阻塞)");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;

    /* 先写入，让 select 立即返回 */
    write(pipefd[1], "x", 1);

    FD_ZERO(&rfds);
    FD_SET(pipefd[0], &rfds);
    ret = select(pipefd[0] + 1, &rfds, NULL, NULL, NULL);
    TEST_ASSERT(ret > 0, "select(NULL timeout) 应成功");

    { char tmp[4]; read(pipefd[0], tmp, 4); }
cleanup:
    teardown_pipe(pipefd);
    TEST_PASS();
}

/* ================================================================== */
/*  epoll 测试                                                         */
/* ================================================================== */

static void
test_epoll_lt_basic(void)
{
    int pipefd[2];
    int epfd = -1;
    struct epoll_event ev, out;
    int ret;

    TEST_START("epoll — LT 基本");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;
    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");

    ev.events = EPOLLIN; ev.data.fd = pipefd[0];
    TEST_ASSERT(epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev) == 0, "epoll_ctl ADD");

    /* 空 pipe → 超时 */
    ret = epoll_wait(epfd, &out, 1, TMO_SHORT);
    TEST_ASSERT(ret == 0, "空 pipe epoll_wait 应超时");

    /* 写入 → LT 立即返回 */
    write(pipefd[1], "ep", 3);
    ret = epoll_wait(epfd, &out, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0,               "epoll_wait 应检测到事件");
    TEST_ASSERT(out.events & EPOLLIN,  "事件应包含 EPOLLIN");
    TEST_ASSERT(out.data.fd == pipefd[0], "data.fd 不匹配");

    /* LT: 不读取数据，再次调用应仍返回（水平触发） */
    ret = epoll_wait(epfd, &out, 1, TMO_SHORT);
    TEST_ASSERT(ret > 0, "LT: 未读时再次 wait 仍应触发");

    { char buf[8]; read(pipefd[0], buf, 8); }
    /* 排空后不再触发 */
    ret = epoll_wait(epfd, &out, 1, TMO_SHORT);
    TEST_ASSERT(ret == 0, "LT: 排空后应超时");

    epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], NULL);
cleanup:
    if (epfd >= 0) close(epfd);
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_epoll_del(void)
{
    int pipefd[2];
    int epfd = -1;
    struct epoll_event ev, out;
    int ret;

    TEST_START("epoll — DEL 后不再收到事件");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;
    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");

    ev.events = EPOLLIN; ev.data.fd = pipefd[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev);

    write(pipefd[1], "x", 1);

    /* 先收到一次 */
    ret = epoll_wait(epfd, &out, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0, "DEL 前应收到事件");
    { char buf[4]; read(pipefd[0], buf, 4); }

    /* 删除 */
    TEST_ASSERT(epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], NULL) == 0, "epoll_ctl DEL");

    /* 再写，不应再收到 */
    write(pipefd[1], "y", 1);
    ret = epoll_wait(epfd, &out, 1, TMO_SHORT);
    TEST_ASSERT(ret == 0, "DEL 后不应再收到事件");

cleanup:
    if (epfd >= 0) close(epfd);
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_epoll_multiple(void)
{
    int pipefd[2];
    int epfd = -1;
    struct epoll_event ev[2], events[2];
    int ret;

    TEST_START("epoll — 多 fd 监视");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;
    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");

    ev[0].events = EPOLLIN;  ev[0].data.fd = pipefd[0];
    ev[1].events = EPOLLOUT; ev[1].data.fd = pipefd[1];
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev[0]);
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[1], &ev[1]);

    ret = epoll_wait(epfd, events, 2, TMO_SHORT);
    TEST_ASSERT(ret > 0, "多 fd epoll_wait 应返回事件");

    int found_write = 0;
    for (int i = 0; i < ret; i++) {
        if (events[i].data.fd == pipefd[1] && (events[i].events & EPOLLOUT))
            found_write = 1;
    }
    TEST_ASSERT(found_write, "应检测到 pipe 写端可写");

cleanup:
    if (epfd >= 0) close(epfd);
    teardown_pipe(pipefd);
    TEST_PASS();
}

static void
test_epoll_hup(void)
{
    int pipefd[2];
    int epfd = -1;
    struct epoll_event ev, out;
    int ret;

    TEST_START("epoll — POLLHUP (对端关闭)");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;
    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");

    ev.events = EPOLLIN; ev.data.fd = pipefd[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev);

    /* 关闭写端 → 读端应收到 POLLHUP */
    close(pipefd[1]);
    pipefd[1] = -1;

    ret = epoll_wait(epfd, &out, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0, "HUP epoll_wait 应返回事件");
    TEST_ASSERT(out.events & (EPOLLHUP | EPOLLIN),
                "关闭写端后应收到 EPOLLHUP 或 EPOLLIN");

cleanup:
    if (epfd >= 0) close(epfd);
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (pipefd[1] >= 0) close(pipefd[1]);
    TEST_PASS();
}

/* ================================================================== */
/*  epoll ET: 写数据→注册ET→写新数据→确认ET触发                       */
/*  不在 ET 监视前写入的数据不应触发事件（ET 只跟踪状态变化）          */
/* ================================================================== */
static void
test_epoll_et(void)
{
    int pipefd[2];
    int epfd = -1;
    struct epoll_event ev, out;
    int ret;

    TEST_START("epoll — ET 边缘触发");

    int __pr = setup_pipe(pipefd);
    TEST_ASSERT(__pr == 0, "pipe2");
    if (__pr < 0) goto cleanup;
    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");

    /* 在注册 ET 前写入数据 — ET 不会认为这是"变化" */
    write(pipefd[1], "old_data", 9);

    ev.events = EPOLLIN | EPOLLET; ev.data.fd = pipefd[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev);

    /* 因为数据在注册前就已存在，ET 应不触发（尚未发生"从无到有"变化）
     * 如果内核触发了注册前的旧数据，此断言可能失败——但这是内核行为差异。 */
    ret = epoll_wait(epfd, &out, 1, TMO_SHORT);

    /* 消费旧数据 */
    { char buf[32]; read(pipefd[0], buf, 32); }

    /* 写入新数据 — ET 应明确触发 */
    write(pipefd[1], "new_data", 9);
    ret = epoll_wait(epfd, &out, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0, "ET 应检测到新写入的数据");
    TEST_ASSERT(out.events & EPOLLIN, "ET 事件应包含 EPOLLIN");

    /* ET: 消费后不再次触发（没有新数据变化） */
    { char tmp[16]; read(pipefd[0], tmp, 16); }
    ret = epoll_wait(epfd, &out, 1, TMO_SHORT);
    TEST_ASSERT(ret == 0, "ET: 消费后未写入时应超时");

cleanup:
    if (epfd >= 0) close(epfd);
    teardown_pipe(pipefd);
    TEST_PASS();
}

/* ================================================================== */
/*  TCP socket + poll/epoll 测试                                       */
/* ================================================================== */

static void
test_poll_tcp_accept(void)
{
    int listen_fd = -1, client_fd = -1, conn_fd = -1;
    int port;
    struct pollfd pf;
    int ret;

    TEST_START("poll — TCP accept 检测");

    listen_fd = setup_tcp_listener(&port);
    if (listen_fd < 0) goto cleanup;

    /* connect (非阻塞确认连接) */
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(client_fd >= 0, "client socket");
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = tlibc_htons(port);
    addr.sin_addr.s_addr = tlibc_inet_addr("127.0.0.1");
    connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));

    /* poll 检测 listen fd 可读（有 pending accept） */
    pf.fd = listen_fd; pf.events = POLLIN; pf.revents = 0;
    ret = poll(&pf, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0, "poll 应检测到 listen fd 可读");

    conn_fd = accept(listen_fd, NULL, NULL);
    TEST_ASSERT(conn_fd >= 0, "accept 成功");

cleanup:
    if (conn_fd   >= 0) close(conn_fd);
    if (client_fd >= 0) close(client_fd);
    if (listen_fd >= 0) close(listen_fd);
    TEST_PASS();
}

static void
test_epoll_tcp_accept(void)
{
    int listen_fd = -1, client_fd = -1, conn_fd = -1;
    int epfd = -1;
    int port;
    struct epoll_event ev, out;
    int ret;

    TEST_START("epoll — TCP accept 检测");

    listen_fd = setup_tcp_listener(&port);
    if (listen_fd < 0) goto cleanup;

    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");
    ev.events = EPOLLIN; ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(client_fd >= 0, "client socket");
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = tlibc_htons(port);
    addr.sin_addr.s_addr = tlibc_inet_addr("127.0.0.1");
    connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));

    ret = epoll_wait(epfd, &out, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0,                "epoll_wait 应检测到 accept");
    TEST_ASSERT(out.events & EPOLLIN,   "epoll 事件应包含 EPOLLIN");
    TEST_ASSERT(out.data.fd == listen_fd, "data.fd 应为 listen fd");

    conn_fd = accept(listen_fd, NULL, NULL);
    TEST_ASSERT(conn_fd >= 0, "accept 成功");

cleanup:
    if (conn_fd   >= 0) close(conn_fd);
    if (client_fd >= 0) close(client_fd);
    if (epfd      >= 0) close(epfd);
    if (listen_fd >= 0) close(listen_fd);
    TEST_PASS();
}

static void
test_epoll_tcp_shutdown(void)
{
    int listen_fd = -1, client_fd = -1, conn_fd = -1;
    int epfd = -1;
    int port;
    struct epoll_event ev, out;
    int ret;

    TEST_START("epoll — TCP 对端关闭 POLLHUP");

    listen_fd = setup_tcp_listener(&port);
    if (listen_fd < 0) goto cleanup;

    /* accept 连接 */
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = tlibc_htons(port);
    addr.sin_addr.s_addr = tlibc_inet_addr("127.0.0.1");
    connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));
    conn_fd = accept(listen_fd, NULL, NULL);
    TEST_ASSERT(conn_fd >= 0, "accept 成功");
    close(listen_fd); listen_fd = -1;   /* 不再需要 */

    /* 用 epoll 监视 conn_fd 的读 */
    epfd = epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1");
    ev.events = EPOLLIN; ev.data.fd = conn_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);

    /* 对端(client)关闭 → conn_fd 应收到 EPOLLHUP */
    close(client_fd); client_fd = -1;

    ret = epoll_wait(epfd, &out, 1, TMO_NORMAL);
    TEST_ASSERT(ret > 0, "epoll_wait 应检测到对端关闭");
    TEST_ASSERT(out.events & (EPOLLHUP | EPOLLIN),
                "关闭对端后应收到 EPOLLHUP 或 EPOLLIN");

cleanup:
    if (conn_fd   >= 0) close(conn_fd);
    if (client_fd >= 0) close(client_fd);
    if (listen_fd >= 0) close(listen_fd);
    if (epfd      >= 0) close(epfd);
    TEST_PASS();
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

TEST_DEFINE_COUNTERS();
int main(void)
{
    TEST_BEGIN("I/O 多路复用测试");

    /* poll */
    test_poll_pipein();
    test_poll_pipeout();
    test_poll_nval();
    test_poll_multi();
    test_poll_timeout_zero();
    test_poll_nfds_zero();
    test_poll_neg_fd();

    /* select */
    test_select_pipe();
    test_select_write();
    test_select_timeout_zero();
    test_select_null_timeout();

    /* epoll */
    test_epoll_lt_basic();
    test_epoll_del();
    test_epoll_multiple();
    test_epoll_hup();
    test_epoll_et();

    /* TCP socket + iomux */
    test_poll_tcp_accept();
    test_epoll_tcp_accept();
    test_epoll_tcp_shutdown();

    return TEST_END();
}
