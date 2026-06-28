#ifndef __TERMIOS_H
#define __TERMIOS_H

#define NCCS 19
struct termios {
    unsigned int c_iflag;       /* 输入模式 */
    unsigned int c_oflag;       /* 输出模式 */
    unsigned int c_cflag;       /* 控制模式（波特率、数据位、停止位等） */
    unsigned int c_lflag;       /* 本地模式（回显、信号、规范输入等） */
    unsigned char c_line;       /* 行规程（通常 N_TTY=0） */
    unsigned char c_cc[NCCS];   /* 控制字符（VINTR, VQUIT, VERASE, VMIN, VTIME ...） */
};

/* ── ioctl 请求 ── */
#define TCGETS      0x5401     /* 读取 termios */
#define TCSETS      0x5402     /* 立即写入 termios */
#define TCSETSW     0x5403     /* 等输出队列排空后写入 */
#define TCSETSF     0x5404     /* 等输出队列排空后丢弃输入再写入 */

/* ── 本地模式标志 c_lflag ── */
/* 控制终端驱动的基本行为：回显、信号生成、行缓冲等 */
#define ISIG        0x00000001 /* 启用信号生成：Ctrl+C→SIGINT, Ctrl+\→SIGQUIT, Ctrl+Z→SIGTSTP */
#define ICANON      0x00000002 /* 规范模式：行缓冲，按 Enter 提交，支持行编辑（退格等） */
#define ECHO        0x00000008 /* 回显输入字符 */
#define ECHOE       0x00000010 /* 规范模式下退格显示为 BS SP BS（擦除刚回的显字符） */
#define ECHOK       0x00000020 /* 规范模式下删除整行时回显（含被删内容） */
#define ECHONL      0x00000040 /* 规范模式下即使 ECHO=0 也回显换行符 */
#define IEXTEN      0x00008000 /* 启用扩展功能（Ctrl+V 字面引用等） */

/* ── 控制字符索引（c_cc[] 下标）── */
#define VMIN        6          /* 非规范模式：最少读取字节数 */
#define VTIME       5          /* 非规范模式：超时（0.1 秒为单位） */
#define VINTR       0          /* Ctrl+C → SIGINT */
#define VQUIT       1          /* Ctrl+\ → SIGQUIT */
#define VERASE      2          /* 退格键 */
#define VKILL       3          /* 删除行 */
#define VEOF        4          /* Ctrl+D → EOF */
#define VSTART      8          /* Ctrl+Q → XON 恢复输出 */
#define VSTOP       9          /* Ctrl+S → XOFF 暂停输出 */
#define VSUSP      10          /* Ctrl+Z → SIGTSTP */
#define VREPRINT   12          /* Ctrl+R → 重新打印当前行 */

/* ── 输入模式标志 c_iflag ── */
/* 控制输入字符的预处理：映射、过滤、流控 */
#define BRKINT     0x00000002  /* BREAK 条件触发 SIGINT */
#define ICRNL      0x00000100  /* 输入 \r → \n 映射（Enter 键统一成 NL） */
#define INPCK      0x00000010  /* 启用奇偶校验检查 */
#define ISTRIP     0x00000020  /* 丢弃输入字节的高位（7 位模式） */
#define IXON       0x00000400  /* 启用 XON/XOFF 输出流控（Ctrl+S/Q 暂停/恢复输出） */
#define IGNBRK     0x00000001  /* 忽略 BREAK 条件 */
#define IGNPAR     0x00000004  /* 忽略奇偶校验错误 */
#define PARMRK     0x00000008  /* 标记奇偶校验错误（插入 0xFF 0x00） */
#define IXOFF      0x00001000  /* 启用 XON/XOFF 输入流控 */
#define IMAXBEL    0x00002000  /* 输入队列满时响铃 */

/* ── 输出模式标志 c_oflag（仅当 OPOST 置位时生效）── */
/* 控制输出字符的转换：换行符映射、大小写、制表符展开等 */
#define OPOST       0x00000001 /* 启用输出处理（主开关）               */
#define ONLCR       0x00000004 /* NL → CR-NL：输出 \n 时自动补 \r     */
#define OCRNL       0x00000008 /* CR → NL：输出 \r 时映射为 \n        */
#define ONOCR       0x00000010 /* 第 0 列时不输出 \r                  */
#define ONLRET      0x00000020 /* NL 兼作回车功能（同 \r）            */
#define OFILL       0x00000040 /* 用填充字符代替延时                   */
#define TABDLY      0x00000300 /* 制表符延时掩码                       */
#define TAB3        0x00000300 /* 制表符展开为空格                     */

/* ── 控制模式标志 c_cflag ── */
/* 控制硬件：数据位、停止位、奇偶校验、波特率、流控 */
#define CSIZE       0x00000030 /* 数据位掩码                           */
#define CS5         0x00000000 /* 5 位数据 */
#define CS6         0x00000010 /* 6 位数据 */
#define CS7         0x00000020 /* 7 位数据 */
#define CS8         0x00000030 /* 8 位数据 */
#define CSTOPB      0x00000040 /* 2 位停止位（否则 1 位）             */
#define CREAD       0x00000080 /* 启用接收器                          */
#define PARENB      0x00000100 /* 启用奇偶校验                        */
#define PARODD      0x00000200 /* 奇校验（否则偶校验）                 */
#define HUPCL       0x00000400 /* 关闭时挂断                          */
#define CLOCAL      0x00000800 /* 忽略调制解调器控制线（本地连接）     */
#define CRTSCTS     0x80000000 /* 硬件 RTS/CTS 流控                   */

#endif /* __TERMIOS_H */
