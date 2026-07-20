#include "core.h"
#include "tty.h"
#include "termios.h"
#include "terminal_esc.h"
#include "tlibc_print.h"

/*
 * Per-fd saved termios for restore.
 * In practice only fd 0 (stdin) is used for terminal control across the
 * codebase, so a single slot suffices. The fork() model is safe because
 * the child gets its own copy via COW — the parent's flag is unaffected
 * when the child calls restore_term before exec.
 */
static struct {
    struct termios term;
    int           valid;
} saved[16];

/* ---- helpers ---- */

int tlibc_get_term_size(int fd, struct winsize *term)
{
    if (__ioctl(fd, TIOCGWINSZ, term) < 0) {
        return -1;
    }
    return 0;
}

int tlibc_check_term_size(int fd, int row_wanted, int col_wanted)
{
    struct winsize w;
    if (__ioctl(fd, TIOCGWINSZ, &w) < 0) {
        __printf("获取终端大小失败\n");
        __exit(-1);
    }
    if(w.ws_row < row_wanted)
    {
        __printf("终端行数是%d, 小于要求的%d\n", w.ws_row, row_wanted);
        __exit(-1);
    }
    if(w.ws_col < col_wanted)
    {
        __printf("终端列数是%d, 小于要求的%d\n", w.ws_col, col_wanted);
        __exit(-1);
    }
    return 0;
}

int tlibc_get_term_config(int fd, struct termios *term)
{
    if (__ioctl(fd, TCGETS, term) < 0) {
        return -1;
    }
    return 0;
}

int tlibc_set_term_config(int fd, struct termios *term)
{
    if (__ioctl(fd, TCSETS, term) < 0) {
        return -1;
    }
    return 0;
}

/*
 * 设置完整 raw 模式（cfmakeraw 风格），并保存原始配置。
 *
 * 清除的标志（对应 Linux cfmakeraw）：
 *   c_iflag: ICRNL, IXON, BRKINT, INPCK, ISTRIP
 *   c_oflag: OPOST
 *   c_lflag: ICANON, ECHO, ECHOE, ECHOK, ECHONL, ISIG, IEXTEN
 *   c_cflag: 设置 CS8、CREAD
 *   c_cc:    VMIN=1, VTIME=0
 */
int tlibc_set_term_raw_and_noecho(int fd)
{
    if (fd < 0 || fd >= 16) return -1;

    struct termios term;
    if (tlibc_get_term_config(fd, &term) < 0)
        return -1;

    /* 保存原始配置 */
    saved[fd].term = term;
    saved[fd].valid = 1;

    /* 应用 raw 模式 — 保留 OPOST/ONLCR 以便 \n → \r\n 输出正常 */
    term.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    term.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);
    term.c_cflag |= CS8 | CREAD;
    term.c_cc[VMIN]  = 1;
    term.c_cc[VTIME] = 0;

    if (tlibc_set_term_config(fd, &term) < 0)
        return -2;

    return 0;
}

/*
 * 从 saved[] 完全恢复终端配置。
 * 如果没有调用过 set_term_raw_and_noecho，则是空操作。
 */
int tlibc_restore_term(int fd)
{
    if (fd < 0 || fd >= 16) return -1;
    if (!saved[fd].valid)
        return 0; /* 没设置过，跳过 */

    if (tlibc_set_term_config(fd, &saved[fd].term) < 0)
        return -2;

    saved[fd].valid = 0;
    return 0;
}

/* ---- 光标定位（运行时参数，不能用编译期字符串化宏） ---- */

void tlibc_cursor_goto(int row, int col)
{
    __printf("\033[%d;%dH", row, col);
}

void tlibc_cursor_goto_col(int col)
{
    __printf("\033[%dG", col);
}

/**
 * @brief LEGACY — 旧版终端输入处理（基于 stdin + termios raw mode）
 *
 * 通过 pipe 将方向键等转义序列转换为 KEY_UP/DOWN/LEFT/RIGHT 单字节发送。
 * vim / top / pacman 等老旧程序仍在使用。
 *
 * 新程序（fb_* 图形程序）应使用 evdev（/dev/input/event*，参见 linux_input.h）。
 */
int tlibc_general_input_process(int pipe_write_fd)
{
    while(1)
    {
        char input[16];
        int ret = __read(0, input, 3); //阻塞读取
        if(ret == 1)
        {
            int wret = __write(pipe_write_fd, input, 1);
            if (wret < 0) {
                /* 父进程已退出，管道破裂 */
                __exit(0);
            }
            if(input[0] == 'q')
                __exit(0);
        }
        else if(ret == 2)
        {
            /* 两次read得到2字节？视为出错，发送 'q' 退出 */
            input[0] = 'q';
            __write(pipe_write_fd, input, 1);
            __exit(0);
        }
        else if(ret == 3) //1次三个字节，是转义序列
        {
            if(input[0] == 27)//0x1b(ESC) 转义序列开头
            {
                if(input[1] == 91) //0x5b [
                {
                    switch (input[2])
                    {
                    case 0x41: //A, 方向上
                        input[0] = KEY_UP;
                        __write(pipe_write_fd, input, 1);
                        break;
                    case 0x42: //B, 方向下
                        input[0] = KEY_DOWN;
                        __write(pipe_write_fd, input, 1);
                        break;
                    case 0x43: //C, 方向右
                        input[0] = KEY_RIGHT;
                        __write(pipe_write_fd, input, 1);
                        break;
                    case 0x44: //D, 方向左
                        input[0] = KEY_LEFT;
                        __write(pipe_write_fd, input, 1);
                        break;
                    default:
                        continue; //忽略
                        break;
                    }
                }
            }
        }
    }
    __exit(0);
    return 0;
}