#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413

//命令行的空战游戏
void __game_space_invader(int argc, char *argv[])
{
    struct winsize w;

    // 读取终端的行数和列数，这对游戏渲染很重要
    if (__ioctl(1, TIOCGWINSZ, &w) < 0) {
    panic("获取终端信息失败\n"); // 失败
    }
    __printf("获取到终端宽度: %d, 长度: %d\n", w.ws_row, w.ws_col);
    __printf("space invader正在开发中, 敬请期待!\n");
    __exit(0);
}

//游戏管理程序
void game(int argc, char *argv[])
{
    __printf("正在启动space invader...\n");
    __game_space_invader(argc, argv);
}
