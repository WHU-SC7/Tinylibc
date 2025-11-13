//终端的库函数
#ifndef __TERMCTL_H
#define __TERMCTL_H

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413//获取终端大小
#define TIOCSWINSZ	0x5414//设置终端大小

int tlibc_get_term_size(int fd, struct winsize *term);
int tlibc_check_term_size(int fd, int row_wanted, int col_wanted);
int tlibc_set_term_raw_and_noecho(int fd);
int tlibc_restore_term(int fd);

//自定义键值
#define KEY_UP     0x11    // DC1 - 设备控制1
#define KEY_DOWN   0x12    // DC2 - 设备控制2  
#define KEY_LEFT   0x13    // DC3 - 设备控制3
#define KEY_RIGHT  0x14    // DC4 - 设备控制4

int general_input_process(int pipe_write_fd);


#endif