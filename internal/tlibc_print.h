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

#endif