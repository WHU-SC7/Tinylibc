#ifndef __TLIBC_TYPES_H_
#define __TLIBC_TYPES_H_

typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned int mode_t;
typedef unsigned int nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned long off_t;
typedef unsigned int blksize_t;
typedef unsigned int blkcnt_t;

//riscv64的类型定义
typedef long ssize_t;           // 64位有符号，用于可能出错的大小
typedef int pid_t;
typedef unsigned long   size_t; // 64位无符号，用于大小和计数

typedef unsigned long time_t;
typedef int clockid_t;

#endif