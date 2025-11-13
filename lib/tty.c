#include "core.h"
#include "tty.h"
#include "tlibc_ioctl.h"
#include "tlibc_print.h"

int tlibc_get_term_size(int fd, struct winsize *term)
{
    if (__ioctl(fd, TIOCGWINSZ, term) < 0) {
        return -1;
    }
    return 0;
}

//这个函数假设__printf可用
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

// 关闭规范模式和回显
int tlibc_set_term_raw_and_noecho(int fd)
{
    struct termios term;
    if (tlibc_get_term_config(fd, &term) < 0) {
        return -1;
    }
    term.c_lflag &= ~(ICANON | ECHO);
    if (tlibc_set_term_config(fd, &term) < 0) {
        return -2;
    }
    return 0;
}

//恢复到规范和回显模式
int tlibc_restore_term(int fd)
{
    struct termios term;
    if (tlibc_get_term_config(fd, &term) < 0) {
        return -1;
    }
    term.c_lflag |= (ICANON | ECHO);
    if (tlibc_set_term_config(fd, &term) < 0) {
        return -2;
    }
    return 0;
}

/**
 * @brief 让子进程执行这个函数，子进程恢一直从终端读取输入，
 *          每读取到一次就会写入给定的管道的写入端
 *          支持方向键的转义序列，会转换成KEY_UP等自定义键值
 */
int general_input_process(int pipe_write_fd)
{
    while(1)
    {
        char input[16];
        int ret = __read(0, input, 3); //阻塞读取
        if(ret == 1)
        {
            int ret = __write(pipe_write_fd, input, 1);
            if(input[0] == 'q')
                __exit(0);
            if(ret != 1)
                panic("[input_process]ret: %d", ret);
            // __write(0, &input, 1);
        }
        else if(ret == 2)
        {
            // __creat("vim出现错误!", 0644);
            input[0] = 'q';
            __write(pipe_write_fd, input, 1);
            __exit(0);
        }
        else if(ret == 3) //1次三个字节，是转义序列
        {
            if(input[0] == 27)//0x1b(ESC) 转义序列开头，读取完整的序列
            {
                // char tmp;
                // __write(pipe_write_fd, &input, 1);
                // __read(0, &tmp, 1);
                if(input[1] == 91) //0x5b [
                {
                    // __read(0, &tmp, 1);
                    switch (input[2])
                    {
                    case 0x41: //A, 方向上
                        input[0] = KEY_UP;
                        __write(pipe_write_fd, input, 1);
                        // __creat("转义序列上", 0644);
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