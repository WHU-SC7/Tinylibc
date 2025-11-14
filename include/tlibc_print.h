// tlibc使用，底层是__printf
#ifndef __PRINT_H__
#define __PRINT_H__
#define COLOR_RESET    "\033[0m"  // 重置所有属性
#define RED_COLOR_PRINT		"\033[31;1m"
#define GREEN_COLOR_PRINT	"\033[32;1m"
#define YELLOW_COLOR_PRINT	"\033[33;1m"
#define BLUE_COLOR_PRINT		"\033[34;1m"
#define MAGANTA_COLOR_PRINT 	"\033[35;1m"
#define CYAN_COLOR_PRINT		"\033[36;1m"
#define CLEAR_COLOR_PRINT	"\033[0m"

#define BRIGHT_RED_COLOR_PINRT	   "\033[91m"
#define BRIGHT_GREEN_COLOR_PRINT   "\033[92m"
#define BRIGHT_YELLOW_COLOR_PRINT  "\033[93m"
#define BRIGHT_BLUE_COLOR_PRINT	   "\033[94m"
#define BRIGHT_MAGANTA_COLOR_PRINT "\033[95m"
#define BRIGHT_CYAN_COLOR_PINRT	   "\033[96m"

#define PRINT_COLOR(color, format, ...) \
    do { \
        __printf("%s" format "%s", color, ##__VA_ARGS__, COLOR_RESET); \
    } while(0)  // do-while结构避免宏展开问题

// 默认日志宏（蓝色，INFO级别）
#define LOG(format, ...) \
    PRINT_COLOR(BLUE_COLOR_PRINT, "[Tlibc INFO][%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__)

#define panic(format, ...) \
    do { \
        __printf("%s" "[Tlibc panic][%s:%d] " format "%s", RED_COLOR_PRINT, __FILE__, __LINE__, ##__VA_ARGS__, COLOR_RESET); \
        while(1) \
            ; \
    } while(0)

/* 256 色前景/背景宏 */
#define FG_BLACK   16
#define FG_WHITE   231
#define FG_RED     196
#define FG_GREEN   46
#define FG_YELLOW  226
#define FG_BLUE    21
#define FG_MAGENTA 201
#define FG_CYAN    51

#define BG_BLACK   16
#define BG_WHITE   231
#define BG_RED     196
#define BG_GREEN   46
#define BG_YELLOW  226
#define BG_BLUE    21
#define BG_MAGENTA 201
#define BG_CYAN    51

#define SET_ROW_COLOR(row, bg, fg) \
    __printf("\033[%d;1H\033[48;5;%dm\033[38;5;%dm\033[2K", (row), (bg), (fg))

#endif