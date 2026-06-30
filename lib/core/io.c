/* SPDX-License-Identifier: MIT
 *
 * io.c — 文件 I/O、文件系统操作、文件描述符管理的 syscall 包装
 *
 * 对应 core.h 中声明的 __ 前缀函数。
 * 约定：返回 Linux 内核风格负 errno（-ENOENT, -EINVAL 等）。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"

/* ── 基础 I/O ── */

ssize_t __write(int fd, const void *buf, int len)
{
    return syscall(SYS_write, fd, buf, len);
}

ssize_t __read(int fd, const void *buf, int len)
{
    return syscall(SYS_read, fd, buf, len);
}

int __openat(int fd, const char *pathname, int flags, unsigned short mode)
{
    return syscall(SYS_openat, fd, pathname, flags, mode);
}

int __creat(const char *pathname, unsigned short mode)
{
    return syscall(SYS_openat, AT_FDCWD, pathname,
                   O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int __close(int fd)
{
    return syscall(SYS_close, fd);
}

/* ── 文件描述符操作 ── */

off_t __lseek(int fd, off_t offset, int whence)
{
    return syscall(SYS_lseek, fd, offset, whence);
}

int __ftruncate(int fd, off_t length)
{
    return syscall(SYS_ftruncate, fd, length);
}

int __ioctl(int fd, unsigned long request, void *argp)
{
    return syscall(SYS_ioctl, fd, request, argp);
}

int __pipe2(int pipefd[2], int flags)
{
    return syscall(SYS_pipe2, pipefd, flags);
}

int __fcntl(int fd, int cmd, unsigned long arg)
{
    return syscall(SYS_fcntl, fd, cmd, arg);
}

int __dup(int oldfd)
{
    return syscall(SYS_dup, oldfd);
}

int __dup2(int oldfd, int newfd)
{
    return syscall(SYS_dup2, oldfd, newfd);
}

int __dup3(int oldfd, int newfd, int flags)
{
    return syscall(SYS_dup3, oldfd, newfd, flags);
}

ssize_t __readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
    return syscall(SYS_readlinkat, dirfd, pathname, buf, bufsiz);
}

/* ── 目录 / 文件系统 ── */

long __getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count)
{
    return syscall(SYS_getdents64, fd, dirp, count);
}

int __fstat(int fd, struct stat *statbuf)
{
    return syscall(SYS_fstat, fd, statbuf);
}

int __unlinkat(int dirfd, const char *pathname, int flags)
{
    return syscall(SYS_unlinkat, dirfd, pathname, flags);
}

char *__getcwd(char *buf, size_t size)
{
    return (char *)syscall(SYS_getcwd, buf, size);
}

int __chdir(const char *path)
{
    return syscall(SYS_chdir, path);
}

int __mkdirat(int dirfd, const char *pathname, mode_t mode)
{
    return syscall(SYS_mkdirat, dirfd, pathname, mode);
}

int __rmdir(const char *pathname)
{
    return syscall(SYS_unlinkat, AT_FDCWD, pathname, AT_REMOVEDIR);
}

int __renameat(int olddirfd, const char *oldpath,
               int newdirfd, const char *newpath)
{
    return syscall(SYS_renameat2, olddirfd, oldpath, newdirfd, newpath, 0);
}

int __rename(const char *oldpath, const char *newpath)
{
    return syscall(SYS_renameat2, AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

/* ── 文件元数据 ── */

int tlibc_chmod(const char *pathname, mode_t mode)
{
    return syscall(SYS_chmod, pathname, mode);
}

int tlibc_stat(const char *pathname, struct stat *statbuf)
{
    return syscall(SYS_stat, pathname, statbuf);
}

/* ── 随机数 ── */

ssize_t __getrandom(void *buf, size_t buflen, unsigned int flags)
{
    return syscall(SYS_getrandom, buf, buflen, flags);
}

/* ── statfs（文件系统信息） ── */

int __statfs(const char *pathname, struct statfs *buf)
{
    return (int)syscall(SYS_statfs, pathname, buf);
}

int __fstatfs(int fd, struct statfs *buf)
{
    return (int)syscall(SYS_fstatfs, fd, buf);
}

/* ── I/O 多路复用 ── */

int __poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return (int)syscall(SYS_poll, fds, nfds, timeout);
}

int __ppoll(struct pollfd *fds, nfds_t nfds,
            const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    return (int)syscall(SYS_ppoll, fds, nfds, timeout_ts, sigmask, 8);
}

int __pselect6(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, const struct timespec *timeout,
               const sigset_t *sigmask)
{
    struct {
        const sigset_t *ss;
        size_t ss_len;
    } sigdata = { sigmask, 8 };
    return (int)syscall(SYS_pselect6, nfds, readfds, writefds,
                        exceptfds, timeout, &sigdata);
}

int __epoll_create1(int flags)
{
    return (int)syscall(SYS_epoll_create1, flags);
}

int __epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return (int)syscall(SYS_epoll_ctl, epfd, op, fd, event);
}

int __epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return (int)syscall(SYS_epoll_wait, epfd, events, maxevents, timeout);
}

int __epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                  int timeout, const sigset_t *sigmask)
{
    return (int)syscall(SYS_epoll_pwait, epfd, events, maxevents, timeout, sigmask, 8);
}
