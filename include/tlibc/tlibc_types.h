#ifndef __TLIBC_TYPES_H_
#define __TLIBC_TYPES_H_

typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned int mode_t;
typedef unsigned long nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long off_t;
typedef long blksize_t;
typedef long blkcnt_t;

//riscv64的类型定义
typedef long ssize_t;           // 64位有符号，用于可能出错的大小
typedef int pid_t;
typedef unsigned long   size_t; // 64位无符号，用于大小和计数

typedef long time_t;
typedef int clockid_t;

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int int16_t;
typedef unsigned short int uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long int int64_t;
typedef unsigned long int uint64_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
