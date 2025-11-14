#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"


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

// !!!只有wait4是系统调用，调用号是260. wait,waitpid是wait4的包装
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

void *tlibc_malloc(unsigned long size)
{
    long ret = __brk(0);
    char *ptr  = (char *)__brk((void *)(ret+size)); //分配16k内存示例
    ptr -= size;
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
        time.st_atime_sec = 0;
        time.st_atime_nsec = msecond*1000000;
        __nanosleep(&time, &time);
    }
    else
    {
        time.st_atime_sec = msecond / 1000;
        time.st_atime_nsec = (msecond%1000)*1000000;
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

#define CLONE_VM        0x00000100  /* 共享内存空间 */
#define CLONE_FS        0x00000200  /* 共享工作目录等 */
#define CLONE_FILES     0x00000400  /* 共享文件描述符表 */
#define CLONE_SIGHAND   0x00000800  /* 共享信号处理函数 */
#define CLONE_THREAD    0x00010000  /* 让它成为同进程的线程 */
#define CLONE_FLAGS (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD)
long __clone(unsigned long flags, void *stack, int *parent_tid, int *child_tid, unsigned long tls)
{
    return syscall(SYS_clone, flags, stack, parent_tid, child_tid, tls);
}

long tlibc_clone_thread(void *stack)//创建线程,失败了
{
    return __clone(CLONE_FLAGS, stack, 0, 0, 0);
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

void *__memmove(void *dest, const void *src, size_t n) //简单的memmove, 不考虑重叠情况
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

//printf
void print_int(int num)
{
    
    char buf[32];
    char c;
    int count=0;
    __memset((void *)buf,0,32);

    //处理负数
    if(num < 0)
    {
        num = -num;
        char *negative = "-";
        __write(STDOUT,negative,1);
    }
    if(num == 0)
    {
        count = 1;
        buf[0] = '0';
    }
    while(num!=0)
    {
        c = num % 10; //从i最低位开始，计算每一位的数字
        buf[count++] = c + 48; //向缓冲区写入对应字符，48表示字符0
        num /= 10;
    }

    char tmp;
    for(int i=0; i < count/2; i++) //反转，让字符串顺序正确
    {
        tmp = buf[i];
        buf[i] = buf[(count-1)-i];
        buf[(count-1)-i] = tmp;
    }
    
    __write(STDOUT,buf,count);
}

void print_long(long num)
{
    
    char buf[32];
    char c;
    int count=0;
    __memset((void *)buf,0,32);

    //处理负数
    if(num < 0)
    {
        num = -num;
        char *negative = "-";
        __write(STDOUT,negative,1);
    }
    if(num == 0)
    {
        count = 1;
        buf[0] = '0';
    }
    while(num!=0)
    {
        c = num % 10; //从i最低位开始，计算每一位的数字
        buf[count++] = c + 48; //向缓冲区写入对应字符，48表示字符0
        num /= 10;
    }

    char tmp;
    for(int i=0; i < count/2; i++) //反转，让字符串顺序正确
    {
        tmp = buf[i];
        buf[i] = buf[(count-1)-i];
        buf[(count-1)-i] = tmp;
    }
    
    __write(STDOUT,buf,count);
}

void print_string(const char *str)
{
    int count = 0;
    char *str_calcu = (char *)str; //计算字符个数
    while(*str_calcu++)
        count++;
    __write(STDOUT,str,count);
}

struct my_va_list
{
    unsigned long reg[8];   //riscv有a0-a7共8个参数寄存器
    char *stack_arg;        //栈参数的起始，也可以用来获取a1-a7的参数
    long count;         
};

// #include "print.h" //之后去除，现在__printf基本正确
/**
 * @brief 从栈上获取第一个参数或之后的参数（第0个参数是const char *fmt
 * @param va_list 用va_list->stack_arg来计算参数位置
 * @param num 至少为1
 */
static unsigned long get_reg_arg_from_stack(struct my_va_list *va_list, int num)
{
#if RISCV_TLIBC == 1
    return *(unsigned long *)(va_list->stack_arg+8*num-8*8);
#elif X86_64_TLIBC == 1
    return *(unsigned long *)(va_list->stack_arg+8*num);
#endif
}
#define X86_64_TLIBC 1
/**
 * @brief 获取下一个参数
 */
unsigned long get_va_arg(struct my_va_list *va_list)
{
#if RISCV_TLIBC == 1
    unsigned long ret = get_reg_arg_from_stack(va_list,va_list->count);
    va_list->count++;
    // printf("第%d个参数，返回参数值: %d\n",va_list->count,va_list->reg[va_list->count]);
    return ret;
#elif X86_64_TLIBC == 1
    if(va_list->count < 6) //6个参数寄存器
    {
        int tmp = va_list->count;
        va_list->count++;
        return va_list->reg[tmp];
    }
    else
    {
        unsigned long ret = get_reg_arg_from_stack(va_list,va_list->count - 6*8);
        va_list->count++;
        // printf("第%d个参数，返回参数值: %d\n",va_list->count,va_list->reg[va_list->count]);
        return ret;
    }
#endif
}

//在这次提交的测试函数中，valist的reg中第五个参数保存的不对
void show_va_list_reg(struct my_va_list *va_list)
{
    for(int i=0;i<8;i++)
    {
        // printf("第%d个寄存器: %d\n",i,va_list->reg[i]);
    }
}

//栈上保存的参数从第一个开始都对，第0个不是fmt
void show_va_list_stack(struct my_va_list *va_list)
{
#if RISCV_TLIBC == 1
    for(int i=0;i<12;i++)
    {
        // printf("栈上第%d个参数: %d\n",i,*(unsigned long *)(va_list->stack_arg+8*i-8*8));
    }
#elif X86_64_TLIBC == 1
    for(int i=0;i<12;i++)
    {
        // printf("栈上第%d个参数: %d\n",i,*(unsigned long *)(va_list->stack_arg+8*i));
    }
#endif
}

void __printf(const char *fmt, ...)
{
    // 参数e解析
    struct my_va_list va_list;
#if RISCV_TLIBC == 1
    //初始化va_list
    va_list.count=1;

    // __builtin_frame_address是编译器内置函数，提供函数栈帧地址。栈帧的原理有待确定
    // 总之目前的编译a选项下，这样可以找到栈上参数.第一个是*(unsigned long *)(va_list.stack_arg)
    va_list.stack_arg = (char*)__builtin_frame_address(0)+8*8;

    //把前8个参数保存到my_va_list的reg中，第五个参数保存的不对
    __asm__ volatile (
        "sd a0, 0(%0)\n" "sd a1, 8(%0)\n" "sd a2, 16(%0)\n" "sd a3, 24(%0)\n"
        "sd a4, 32(%0)\n" "sd a5, 40(%0)\n" "sd a6, 48(%0)\n" "sd a7, 56(%0)\n"
        : 
        : "r"(&va_list.reg)
        : "memory"
    );
#elif X86_64_TLIBC == 1
    // x86_64 架构的初始化
    va_list.count = 1;
    
    // 使用内联汇编获取寄存器参数
    __asm__ volatile (
        "movq %%rdi, 0(%0)\n"
        "movq %%rsi, 8(%0)\n" 
        "movq %%rdx, 16(%0)\n"
        "movq %%rcx, 24(%0)\n"
        "movq %%r8, 32(%0)\n"
        "movq %%r9, 40(%0)\n"
        "movq $0, 48(%0)\n"    // 第7个寄存器位置（x86_64只有6个参数寄存器）
        "movq $0, 56(%0)\n"    // 第8个寄存器位置
        : 
        : "r"(&va_list.reg)
        : "memory"
    );
    
    // 计算栈参数的起始位置
    // 在 x86_64 中，栈参数从 rsp+8 开始（跳过返回地址）
    // 但我们需要考虑当前栈帧的情况
    char *frame_addr = (char*)__builtin_frame_address(0);
    // 栈参数通常从 frame_addr + 16 开始（跳过保存的rbp和返回地址）
    va_list.stack_arg = frame_addr + 16;

#else
    // 通用实现或错误处理
    return;
#endif

    //调试时使用
    // show_va_list_reg(&va_list); //显示第五个参数保存的不对
    // show_va_list_stack(&va_list); //栈上保存的参数从第一个开始都对，第0个不是fmt
    
    // 输出,逐段字符输出
    char *str = (char *)fmt;
    while(1)
    {
        if(!*str) // 输出完了
            break;
        if(*str=='%') // 格式化输出一个变量,现在只支持%d
        {
            switch (*++str)
            {
            case 'd':
                print_int(get_va_arg(&va_list));
                break;
            case 'l':
                print_long(get_va_arg(&va_list));
                break;
            case 's':
                //print_string
                char *arg_str = (char *)get_va_arg(&va_list);
                char *tmp = arg_str;
                int count = 0;
                while(*tmp++)
                    count++;
                __write(STDOUT,arg_str,count);
                break;
            default:
                char error_string[3];
                error_string[0] = '%';
                error_string[1] = *str;
                error_string[2] = 0;
                print_string(error_string);
                break;
            }
            str++;
        }
        //输出一整段，直到找到类似%d的格式符或者字符串末尾
        char *str_end = str;
        while(*str_end!='%' && *str_end)
        {
            str_end++;
        }
        int count = str_end-str; //计算输出字符的个数
        __write(STDOUT,str,count);
        str = str_end;
    }
    

}

// SC7在qemu平台自定义的调用
void tlibc_shutdown() //为了避免与user.c的命名冲突加前缀tlibc了
{
#if RISCV_TLIBC == 1
    syscall(SYS_shutdown);
#else
    //在x64主机上，退出进程即可
    __exit(0);
#endif
}