#ifndef __TERMIOS_H
#define __TERMIOS_H

#define NCCS 19
struct termios {
    unsigned int c_iflag;  // 输入模式
    unsigned int c_oflag;  // 输出模式
    unsigned int c_cflag;  // 控制模式
    unsigned int c_lflag;  // 本地模式
    unsigned char c_line;   // 行规程
    unsigned char c_cc[NCCS]; // 控制字符
};

/* ioctl 请求 */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404

/* 本地模式标志 */
#define ICANON      0x00000002
#define ECHO        0x00000008
#define ISIG        0x00000001
#define IEXTEN      0x00008000

/* 控制字符索引 */
#define VMIN        6
#define VTIME       5

/* 输入模式标志 */
#define BRKINT      0x00000002
#define ICRNL       0x00000100
#define INPCK       0x00000010
#define ISTRIP      0x00000020
#define IXON        0x00000400

/* 输出模式标志 */
#define OPOST       0x00000001

#endif /* __TERMIOS_H */
