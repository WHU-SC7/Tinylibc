#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "core.h"


//系统调用包装
//为了避免同名冲突，命名加上下划线
/**
 * @brief 向文件描述符写入
 */
ssize_t __write(int fd, const void *buf, int len)
{
    return syscall(SYS_write,fd,buf,len);
}

/**
 * @brief 从文件描述符读取
 */
ssize_t __read(int fd, const void *buf, int len)
{
    return syscall(SYS_read,fd,buf,len);
}

/**
 * @brief 打开文件，获得一个文件描述符用于后续调用
 */
int __openat(int fd, const char *pathname, int flags, unsigned short mode)
{
    return syscall(SYS_openat, fd, pathname, flags, mode);
}

/**
 * @brief 创建文件 相当于flags是O_CREAT|O_WRONLY|O_TRUNC的openat
 *      详情参见man 2 open的creat条目
 * @param pathname 要创建的文件所在的路径
 * @param mode 创建文件的权限
 */
int __creat(const char *pathname, unsigned short mode)
{
    return syscall(SYS_openat, AT_FDCWD, pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

/**
 * @brief 关闭指定的文件描述符
 */
int __close(int fd)
{
    return syscall(SYS_close, fd);
}

/**
 * @brief 获取目录下的目录项
 * @param fd 目录的fd
 * @param dirp 用户程序用于接收信息的缓冲区
 * @param count 缓冲区的长度
 */
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

/**
 * @param buf 用于存储工作目录字符串的缓冲区
 * @param size 缓冲区大小
 * @return 返回值等于buf
 */
char *__getcwd(char *buf, size_t size)
{
    return (char *)syscall(SYS_getcwd, buf, size);
}

int __chdir(const char *path)
{
    return syscall(SYS_chdir,path);
}

int __mkdirat(int dirfd, const char *pathname, mode_t mode)
{
    return syscall(SYS_mkdirat, dirfd, pathname, mode);
}

int __rmdir(const char *pathname)
{
    return syscall(SYS_unlinkat, AT_FDCWD, pathname, AT_REMOVEDIR);
}

/*按riscv的调用号表，renameat和renameat2是两个调用，不过很相似，而且SC7只实现了一个，所以都用renameat2的调用号*/
int __renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    return syscall(SYS_renameat2, olddirfd, oldpath, newdirfd, newpath, 0);
}

int __rename(const char *oldpath, const char *newpath)
{
    return syscall(SYS_renameat2, AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

/* 下面是进程相关的调用 */
pid_t __fork()
{
    return syscall(SYS_fork);
}

void __exit(int status)
{
    syscall(SYS_exit, status);
}

// NOTE: only wait4 is the raw syscall (nr 260). wait/waitpid are wrappers.
// sys_wait4(pid_t pid, int __user *stat_addr, int options, struct rusage __user *ru)
pid_t __waitpid(int pid, int *wstatus, int options)
{
    return syscall(SYS_wait4, pid, wstatus, options, 0);
}

pid_t __wait(int *wstatus)
{
    return syscall(SYS_wait4, -1, wstatus, 0, 0);
}

int __execve(const char *pathname, char *const argv[],
                  char *const envp[])
{
    return syscall(SYS_execve, pathname, argv, envp);
}

//获取终端长宽
int __ioctl(int fd, unsigned long request, void *argp)
{
    return syscall(SYS_ioctl, fd, request, argp);
}

//内存分配
long __brk(void *addr)
{
    return syscall(SYS_brk, addr);
}

#include "pthread.h"
void *tlibc_malloc(unsigned long size)
{
    // long ret = __brk(0);
    // char *ptr  = (char *)__brk((void *)(ret+size)); //分配16k内存示例
    char *ptr = (char *)__mmap(0, size, PROT_READ|PROT_WRITE, 
                     MAP_PRIVATE|MAP_ANON, -1, 0);
    if(ptr == (char *)-1)
        return (void *)ptr;
    __memset((void *)ptr, 0, size);
    return (void *)ptr;
}

//睡眠
int __nanosleep(const struct timespec *req, struct timespec *rem)
{
    return syscall(SYS_nanosleep, req, rem);
}

//睡眠指定的毫秒数
int tlibc_msleep(unsigned int msecond)
{
    struct timespec time;
    if(msecond < 1000)
    {
        time.tv_sec = 0;
        time.tv_nsec = msecond*1000000;
        __nanosleep(&time, &time);
    }
    else
    {
        time.tv_sec = msecond / 1000;
        time.tv_nsec = (msecond%1000)*1000000;
        __nanosleep(&time, &time);
    }
    return 0;
}

/* Sleeps for the given number of microseconds. Returns 0 on success, or -errno on error */
int tlibc_usleep(unsigned int usecond)
{
    struct timespec time;
    if(usecond < 1000)
    {
        time.tv_sec = 0;
        time.tv_nsec = usecond * 1000;
        __nanosleep(&time, &time);
    }
    else
    {
        time.tv_sec = usecond / 1000000;
        time.tv_nsec = (usecond%1000000)*1000;
        __nanosleep(&time, &time);
    }
    return 0;
}

time_t __time(time_t *tloc)
{
    return syscall(SYS_time, tloc);
}

int __clock_gettime(clockid_t clockid, struct timespec *tp)
{
    return syscall(SYS_clock_gettime, clockid, tp);
}

//信号
int __sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact)
{
    return syscall(SYS_rt_sigaction, signum, act, oldact, 8); //8是信号集大小，在x64是这么多
}

void rt_sig_restore(void)//默认的信号处理返回函数
{
    void __printf(const char *fmt, ...);
    // __printf("进入恢复函数\n");
    __asm__ volatile (
        "mov $15, %%rax\n\t"    // __NR_rt_sigreturn = 15
        "syscall\n\t"           // 64位系统调用
        :
        :
        : "rax", "memory"
    );
}

int tlibc_sigaction(int signum, void (*handler)(int))//包装系统调用，提供设置信号处理函数的接口
{
    struct sigaction sig;

    void *__memset(void *dst, int value, unsigned int n);
    __memset(&sig, 0, sizeof(struct sigaction));
    #define SA_RESTORER   0x04000000
    sig.sa_handler = handler;
    sig.sa_restorer = rt_sig_restore; //默认信号返回函数
    sig.sa_flags = SA_RESTORER; //声明设置了信号返回函数
    unsigned long mask = 0;
    sig.sa_mask.sig[0] = mask;
    return __sigaction(signum, &sig, (void *)0);
}

int __pipe2(int pipefd[2], int flags)//创建管道
{
    return syscall(SYS_pipe2, pipefd, flags);
}

int __yield()//主动让出CPU
{
    return syscall(SYS_sched_yield);
}

pid_t __gettid(void)
{
    return (pid_t)syscall(SYS_gettid);
}

void *__mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset)
{
    return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
}
int __munmap(void *addr, size_t length)
{
    return (int)syscall(SYS_munmap, addr, length);
}

long __futex(unsigned int *uaddr, int futex_op, unsigned int val, const struct timespec *timeout, unsigned int *uaddr2, unsigned int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

pid_t __setsid(void)//不明
{
    return syscall(SYS_setsid);
}

int __rt_sigprocmask(int how, const sigset_t *set,  //信号屏蔽,不明
                         sigset_t *oldset, size_t sigsetsize)
{
    return syscall(SYS_rt_sigprocmask, how, set, oldset, sigsetsize);
}

int __kill(pid_t pid, int sig)//发送信号，不明
{
    return syscall(SYS_kill, pid, sig);
}

pid_t __getpid(void)//获取pid
{
    return syscall(SYS_getpid);
}

//获取随机数
ssize_t __getrandom(void *buf, size_t buflen, unsigned int flags)
{
    return syscall(SYS_getrandom, buf, buflen, flags);
}

off_t __lseek(int fd, off_t offset, int whence)
{
    return syscall(SYS_lseek, fd, offset, whence);
}

int __ftruncate(int fd, off_t length)
{
    return syscall(SYS_ftruncate, fd, length);
}

ssize_t __readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
    return syscall(SYS_readlinkat, dirfd, pathname, buf, bufsiz);
}

int __madvise(void *addr, size_t length, int advice)
{
    return syscall(__NR_madvise, addr, length, advice);
}

void __exit_group(int status)
{
    syscall(__NR_exit_group, status);
}

/* Returns the real user ID of the calling process, or -1 on error */
uid_t tlibc_getuid(void){
    return syscall(SYS_getuid);
}

/* Changes permissions of a file. Returns 0 on success, or -errno on error */
int tlibc_chmod(const char *pathname, mode_t mode){
    return syscall(SYS_chmod, pathname, mode);
}

/* Retrieves file status via stat syscall. Returns 0 on success, or -errno on error */
int tlibc_stat(const char *pathname, struct stat *statbuf){
    return syscall(SYS_stat, pathname, statbuf);
}

//string.h
/**
 * @brief 应为string.h的标准库函数，为了避免同名冲突，命名加上下划线
 */
void *__memset(void *dst, int value, unsigned int n)
{
    char *cdst = (char *)dst;
    unsigned int i;
    for (i = 0; i < n; i++)
    {
        cdst[i] = value;
    }
    return dst;
}

/* Simple memmove. Does NOT handle overlapping regions correctly (uses temp buf). */
void *__memmove(void *dest, const void *src, size_t n)
{
    char *cdest = (char *)dest;
    char *csrc = (char *)src;
    char buf[n];
    for(int i=0; i<n; i++)
    {
        buf[i] = csrc[i];
    }
    for(int i=0; i<n; i++)
    {
        cdest[i] = buf[i];
    }

    return dest;
}

/* Gets the current calendar time. Wraps clock_gettime(CLOCK_REALTIME). Returns 0 on success, or -errno on error */
int tlibc_timespec_get(struct timespec *ts, int base){
    return __clock_gettime(CLOCK_REALTIME, ts);
}