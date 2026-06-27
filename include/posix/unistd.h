#ifndef __UNISTD_H
#define __UNISTD_H

/* lseek 选项 */
#define SEEK_SET    0    /* 绝对偏移 */
#define SEEK_CUR    1    /* 当前偏移量 */
#define SEEK_END    2    /* 从末尾 */

/* 标准文件描述符 */
#define STDIN       0
#define STDOUT      1
#define STDERR      2

/* pipe 读写端索引 */
#define PIPE_READ   0
#define PIPE_WRITE  1

/* 非阻塞标志（用于 pipe/open 等） */
#define O_NONBLOCK  0x800

#endif /* __UNISTD_H */
