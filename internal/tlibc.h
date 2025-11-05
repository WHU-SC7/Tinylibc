#ifndef __TLIBC_H
#define __TLIBC_H
// 临时存放core.c和test.c都需要的宏定义

//type.h
#include "tlibc_types.h"

//放到什么头文件？
#define AT_FDCWD -100 // 当前工作目录

// fcntl.h
#define O_RDONLY 0x000                      ///< 只读
#define O_WRONLY 0x001                      ///< 只写
#define O_RDWR 0x002                        ///< 读写
#define O_CREAT 0100                        ///< 如果指定的文件不存在，则创建该文件。
#define O_CREATE 0100                       ///< 如果指定的文件不存在，则创建该文件。(别名)
// #define O_TRUNC 0x400                       ///< 如果文件已存在且以写方式打开，则将文件长度截断为0，即清空文件内容

// linux的include/uapi/asm-generic/fcntl.h
#define O_TRUNC		00001000	/* not fcntl */
#define O_APPEND        00002000

#define O_DIRECTORY 0200000                 ///< 要求打开的目标必须是一个目录，否则打开失败
#define O_CLOEXEC 02000000                  ///< 在执行 exec 系列函数时，自动关闭该文件描述符（close on exec）

#define AT_REMOVEDIR 0x200 // unlinkat删除目录的选项

// 解析sys_getdents64内容需要
enum
{
    DT_UNKNOWN = 0,
#define DT_UNKNOWN DT_UNKNOWN
    DT_FIFO = 1,
#define DT_FIFO DT_FIFO
    DT_CHR = 2,
#define DT_CHR DT_CHR //字符
    DT_DIR = 4,
#define DT_DIR DT_DIR //目录
    DT_BLK = 6,
#define DT_BLK DT_BLK //块设备
    DT_REG = 8,
#define DT_REG DT_REG //普通文件
    DT_LNK = 10,
#define DT_LNK DT_LNK //符号链接
    DT_SOCK = 12,
#define DT_SOCK DT_SOCK
    DT_WHT = 14
#define DT_WHT DT_WHT
};

//dirent.h
struct linux_dirent64 {
               unsigned long  d_ino;    /* 64-bit inode number */
               unsigned long  d_off;    /* 64-bit offset to next structure */
               unsigned short d_reclen; /* Size of this dirent */
               unsigned char  d_type;   /* File type */
               char           d_name[]; /* Filename (null-terminated) */
           };

//stat,放到什么文件呢？之后参考别的libc吧
struct timespec {
    unsigned long st_atime_sec;
    unsigned long st_atime_nsec;
};
//参考man 2 fstat
#include "stat.h"

//信号结构体
typedef long clock_t;
typedef union sigval {
    int   sival_int;    // 整数值
    void *sival_ptr;    // 指针值
} sigval_t;

typedef struct {
    int      si_signo;     /* 信号编号 */
    int      si_errno;     /* 错误号（通常为0） */
    int      si_code;      /* 信号代码 */
    int      si_trapno;    /* 陷阱号导致硬件信号 */
    pid_t    si_pid;       /* 发送进程的PID */
    uid_t    si_uid;       /* 发送进程的真实用户ID */
    int      si_status;    /* 退出值或信号 */
    clock_t  si_utime;     /* 消耗的用户时间 */
    clock_t  si_stime;     /* 消耗的系统时间 */
    sigval_t si_value;     /* 信号值 */
    int      si_int;       /* POSIX.1b信号 */
    void    *si_ptr;       /* POSIX.1b信号 */
    int      si_overrun;   /* 计时器溢出计数 */
    int      si_timerid;   /* 计时器ID */
    void    *si_addr;      /* 导致故障的内存地址 */
    long     si_band;      /* Band event (POSIX.1b信号) */
    int      si_fd;        /* 文件描述符 */
    short    si_addr_lsb;  /* 地址的最低有效位 */
    void    *si_lower;     /* 冲突访问范围的下限 */
    void    *si_upper;     /* 冲突访问范围的上限 */
    int      si_pkey;      /* 导致访问错误的保护键 */
    void    *si_call_addr; /* 系统调用指令的地址 */
    int      si_syscall;   /* 系统调用编号 */
    unsigned int si_arch;  /* 系统调用的体系结构 */
} siginfo_t;

#define _NSIG       64    // 最大信号数量
#define _NSIG_BPW   64    // 每个字的位数（Bits Per Word）

// 计算需要的字数
#define _NSIG_WORDS (_NSIG / _NSIG_BPW)
typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;
struct sigaction {
               void     (*sa_handler)(int);
               int        sa_flags;
               void     (*sa_restorer)(void);
               sigset_t   sa_mask;
           };

#define SEEK_SET 0//绝对
#define SEEK_CUR 1//当前文件偏移量
#define SEEK_END 2//从末尾

#endif