#ifndef __FCNTL_H
#define __FCNTL_H

/* 来自 tlibc.h: 文件打开/创建标志 */
#define AT_FDCWD (-100)                   /* 当前工作目录 */

#define O_RDONLY     0x000                /* 只读 */
#define O_WRONLY     0x001                /* 只写 */
#define O_RDWR       0x002                /* 读写 */
#define O_CREAT      0100                 /* 不存在则创建 */
#define O_CREATE     0100                 /* 别名 */
#define O_TRUNC      00001000             /* 截断 */
#define O_APPEND     00002000             /* 追加 */
#define O_DIRECTORY  0200000              /* 必须为目录 */
#define O_CLOEXEC    02000000             /* 执行时关闭 */

#define AT_REMOVEDIR 0x200                /* unlinkat 删除目录 */

/* fcntl 命令 */
#define F_DUPFD      0                    /* 复制 fd */
#define F_GETFD      1                    /* 读 fd 标志 */
#define F_SETFD      2                    /* 写 fd 标志 */
#define F_GETFL      3                    /* 读文件状态标志 */
#define F_SETFL      4                    /* 写文件状态标志 */

#endif /* __FCNTL_H */
