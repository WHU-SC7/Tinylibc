#ifndef __TLIBC_IOCTL_H
#define __TLIBC_IOCTL_H


#define ESC             "\033"
#define CSI             ESC "["   // Control Sequence Introducer
#define CURSOR_HOME     CSI "H"           // 移动到左上角 (1,1)
#define CURSOR_UP(n)    CSI #n "A"        // 上移 n 行
#define CURSOR_DOWN(n)  CSI #n "B"        // 下移 n 行  
#define CURSOR_RIGHT(n) CSI #n "C"        // 右移 n 列
#define CURSOR_LEFT(n)  CSI #n "D"        // 左移 n 列
#define CURSOR_POS(row, col) CSI #row ";" #col "H"  // 移动到指定位置
#define CURSOR_HIDE     CSI "?25l"        // 隐藏光标
#define CURSOR_SHOW     CSI "?25h"        // 显示光标
#define CLEAR_SCREEN    CSI "2J"          // 清除整个屏幕
#define CLEAR_FROM_CURSOR CSI "0J"        // 从光标清到屏幕末尾
#define CLEAR_TO_CURSOR   CSI "1J"        // 从屏幕开始清到光标处
#define CLEAR_LINE      CSI "2K"          // 清除整行
#define ALT_SCREEN_ON   ESC "[?1049h"     // 启用替代屏幕缓冲区
#define ALT_SCREEN_OFF  ESC "[?1049l"     // 禁用替代屏幕缓冲区

#define NCCS 19
struct termios {
    unsigned int c_iflag;  // 输入模式
    unsigned int c_oflag;  // 输出模式  
    unsigned int c_cflag;  // 控制模式
    unsigned int c_lflag;  // 本地模式
    unsigned char c_line;   // 行规程
    unsigned char c_cc[NCCS]; // 控制字符
};
// ioctl 请求
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
// 终端标志
#define ICANON      0x00000002
#define ECHO        0x00000008
#define ISIG        0x00000001
// 控制字符索引
#define VMIN        6
#define VTIME       5
// 输入模式
#define BRKINT      0x00000002
#define ICRNL       0x00000100
#define INPCK       0x00000010
#define ISTRIP      0x00000020
#define IXON        0x00000400
// 本地模式
#define IEXTEN      0x00008000
// 输出模式
#define OPOST       0x00000001

#endif