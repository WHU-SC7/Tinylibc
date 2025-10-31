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

//printf
void print_int(int num);
void __printf(const char *fmt, ...);

//自定义
void tlibc_shutdown();
#endif