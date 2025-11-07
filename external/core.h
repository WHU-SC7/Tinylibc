#ifndef __CORE_H
#define __CORE_H
#include "tlibc.h" // for struct linux_dirent64

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
int __nanosleep(const struct timespec *req, struct timespec *rem);
int tlibc_msleep(unsigned int msecond);
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
//clone待验证
long __clone(unsigned long flags, void *stack, int *parent_tid, int *child_tid, unsigned long tls);
long tlibc_clone_thread(void *stack);

//string操作
void *__memset(void *dst, int value, unsigned int n);
void *__memmove(void *dest, const void *src, size_t n);

//printf
void print_int(int num);
void __printf(const char *fmt, ...);

//自定义
void tlibc_shutdown();
#endif