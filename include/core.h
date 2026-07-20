#ifndef __CORE_H
#define __CORE_H

#include "tlibc_types.h"    /* size_t, pid_t, mode_t, off_t, clockid_t */
#include "errno.h"
#include "fcntl.h"          /* O_* flags */
#include "unistd.h"         /* SEEK_*, STDIN, PIPE_* */
#include "dirent.h"         /* struct linux_dirent64 */
#include "time.h"           /* struct timespec */
#include "signal.h"         /* sigset_t, struct sigaction */
#include "mman.h"           /* PROT_*, MAP_*, MAP_FAILED */
#include "stat.h"           /* struct stat — arch 依赖，-I 解析 */

ssize_t __write(int fd, const void *buf, int len);
ssize_t __read(int fd, const void *buf, int len);
int __openat(int fd, const char *pathname, int flags, unsigned short mode);
int __creat(const char *pathname, unsigned short mode);
int __close(int fd);

long __getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
int __fstat(int fd, struct stat *statbuf);
int __unlinkat(int dirfd, const char *pathname, int flags);
char *__getcwd(char *buf, size_t size);
int __chdir(const char *path);
int __mkdirat(int dirfd, const char *pathname, mode_t mode);
int __rmdir(const char *pathname);
int __renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int __rename(const char *oldpath, const char *newpath);

//进程操作
pid_t __fork();
void __exit(int status);
pid_t __waitpid(int pid, int *wstatus, int options);
pid_t __wait(int *wstatus);
int __execve(const char *pathname, char *const argv[], char *const envp[]);

int __ioctl(int fd, unsigned long request, void *argp);

long __brk(void *addr);
void *tlibc_malloc(unsigned long size);
void tlibc_free(void *ptr);
int __nanosleep(const struct timespec *req, struct timespec *rem);
int tlibc_msleep(unsigned int msecond);
int tlibc_usleep(unsigned int usecond);
time_t __time(time_t *tloc);
int __clock_gettime(clockid_t clockid, struct timespec *tp);
int __clock_nanosleep(clockid_t clockid, int flags,
                      const struct timespec *request, struct timespec *remain);
int __sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int tlibc_sigaction(int signum, void (*handler)(int));
int __pipe2(int pipefd[2], int flags);
int __yield();
pid_t __setsid(void);
int __rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset, size_t sigsetsize);
int __kill(pid_t pid, int sig);
pid_t __getpid(void);
ssize_t __getrandom(void *buf, size_t buflen, unsigned int flags);
off_t __lseek(int fd, off_t offset, int whence);
int __ftruncate(int fd, off_t length);
long __readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
int __clone(int (*fn)(void *), void *stack, int flags, void *arg, pid_t *parent_tid, void *tls, pid_t *child_tid);
pid_t __gettid(void);
void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int __munmap(void *addr, size_t length);
long __futex(unsigned int *uaddr, int futex_op, unsigned int val, const struct timespec *timeout, unsigned int *uaddr2, unsigned int val3);
long tlibc_clone_thread(void *stack);
int __madvise(void *addr, size_t length, int advice);
void __exit_group(int status);
uid_t tlibc_getuid(void);
int tlibc_chmod(const char *pathname, mode_t mode);
int tlibc_stat(const char *pathname, struct stat *statbuf);
int __statfs(const char *pathname, struct statfs *buf);
int __fstatfs(int fd, struct statfs *buf);
int __fcntl(int fd, int cmd, unsigned long arg);
int __dup(int oldfd);
int __dup2(int oldfd, int newfd);
int __dup3(int oldfd, int newfd, int flags);

//printf
void tlibc_print_int(int num);
void __printf(const char *fmt, ...);
void __fprintf(int fd, const char *fmt, ...);

#define TIME_UTC 1
int tlibc_timespec_get(struct timespec *ts, int base);
#define printf(fmt, ...) __printf(fmt, ##__VA_ARGS__)
#define fprintf(fd, fmt, ...) __fprintf(fd, fmt, ##__VA_ARGS__)
#define fdprintf(fd, fmt, ...) __fprintf(fd, fmt, ##__VA_ARGS__)

//scanf
int scanf(const char *fmt, ...);

/* main_tid — 主线程 pid（来自 init.h） */
extern pid_t main_tid;

/* ── I/O 多路复用 ── */

/* poll/ppoll need struct pollfd and nfds_t from poll.h */
#include "poll.h"

int __poll(struct pollfd *fds, nfds_t nfds, int timeout);
int __ppoll(struct pollfd *fds, nfds_t nfds,
            const struct timespec *timeout_ts, const sigset_t *sigmask);

/* pselect6 raw syscall */
#include "sys/select.h"
int __pselect6(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, const struct timespec *timeout,
               const sigset_t *sigmask);

/* epoll wrappers */
#include "sys/epoll.h"
int __epoll_create1(int flags);
int __epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int __epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int __epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                  int timeout, const sigset_t *sigmask);

#endif
