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
struct stat {
               dev_t     st_dev;         /* ID of device containing file */
               ino_t     st_ino;         /* Inode number */
               mode_t    st_mode;        /* File type and mode */
               nlink_t   st_nlink;       /* Number of hard links */
               uid_t     st_uid;         /* User ID of owner */
               gid_t     st_gid;         /* Group ID of owner */
               dev_t     st_rdev;        /* Device ID (if special file) */
               off_t     st_size;        /* Total size, in bytes */
               blksize_t st_blksize;     /* Block size for filesystem I/O */
               blkcnt_t  st_blocks;      /* Number of 512B blocks allocated */

               /* Since Linux 2.6, the kernel supports nanosecond
                  precision for the following timestamp fields.
                  For the details before Linux 2.6, see NOTES. */

               struct timespec st_atim;  /* Time of last access */
               struct timespec st_mtim;  /* Time of last modification */
               struct timespec st_ctim;  /* Time of last status change */

           #define st_atime st_atim.tv_sec      /* Backward compatibility */
           #define st_mtime st_mtim.tv_sec
           #define st_ctime st_ctim.tv_sec
           };


#endif