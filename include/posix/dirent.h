#ifndef __DIRENT_H
#define __DIRENT_H

#include "tlibc_types.h"  /* unsigned long */

/* 目录项类型常量 — enum + define 双重定义（POSIX 惯例） */
enum {
    DT_UNKNOWN = 0,
#define DT_UNKNOWN DT_UNKNOWN
    DT_FIFO = 1,
#define DT_FIFO DT_FIFO
    DT_CHR = 2,
#define DT_CHR DT_CHR
    DT_DIR = 4,
#define DT_DIR DT_DIR
    DT_BLK = 6,
#define DT_BLK DT_BLK
    DT_REG = 8,
#define DT_REG DT_REG
    DT_LNK = 10,
#define DT_LNK DT_LNK
    DT_SOCK = 12,
#define DT_SOCK DT_SOCK
    DT_WHT = 14
#define DT_WHT DT_WHT
};

/* Linux getdents64 返回的结构 */
struct linux_dirent64 {
    unsigned long  d_ino;     /* 64-bit inode number */
    unsigned long  d_off;     /* 64-bit offset to next structure */
    unsigned short d_reclen;  /* Size of this dirent */
    unsigned char  d_type;    /* File type */
    char           d_name[];  /* Filename (null-terminated) */
};

#endif /* __DIRENT_H */
