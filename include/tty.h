#ifndef __TTY_H
#define __TTY_H

#include "termios.h"

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ	0x5414

int tlibc_get_term_size(int fd, struct winsize *term);
int tlibc_check_term_size(int fd, int row_wanted, int col_wanted);
int tlibc_get_term_config(int fd, struct termios *term);
int tlibc_set_term_config(int fd, struct termios *term);
int tlibc_set_term_raw_and_noecho(int fd);
int tlibc_restore_term(int fd);

#define KEY_UP     0x11
#define KEY_DOWN   0x12
#define KEY_LEFT   0x13
#define KEY_RIGHT  0x14

int tlibc_general_input_process(int pipe_write_fd);

/* 光标定位（运行时） */
void tlibc_cursor_goto(int row, int col);
void tlibc_cursor_goto_col(int col);

#endif
