#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "core.h"
#include "tlibc_everything.h"

typedef __builtin_va_list my_va_list;
#define my_va_start(v, l)   __builtin_va_start(v, l)
#define my_va_arg(v, t)     __builtin_va_arg(v, t)
#define my_va_end(v)        __builtin_va_end(v)
// #include <stddef.h>

// 字符串缓冲区结构，用于替代直接写文件
typedef struct {
    char *buffer;      // 目标缓冲区
    size_t size;       // 缓冲区总大小
    size_t pos;        // 当前写入位置
    int truncated;     // 是否发生截断
} strbuf_t;

// 向字符串缓冲区写入指定长度的数据
static void strbuf_write(strbuf_t *sb, const char *data, size_t len) {
    if (!sb->buffer || sb->size == 0) {
        // 没有缓冲区或大小为0，只计数
        sb->pos += len;
        sb->truncated = 1;
        return;
    }
    
    size_t remaining = sb->size - sb->pos;
    
    if (len < remaining) {
        // 空间足够
        memcpy(sb->buffer + sb->pos, data, len);
        sb->pos += len;
    } else {
        // 空间不足，只复制能放下的部分
        if (remaining > 0) {
            memcpy(sb->buffer + sb->pos, data, remaining - 1);
            sb->pos = sb->size - 1;
        }
        sb->truncated = 1;
    }
}

// 向字符串缓冲区写入单个字符
static void strbuf_write_char(strbuf_t *sb, char c) {
    strbuf_write(sb, &c, 1);
}

// 将数字转换为字符串并写入缓冲区（有符号）
static void strbuf_write_long(strbuf_t *sb, long num) {
    char buf[32];
    char c;
    int count = 0;
    __memset(buf, 0, 32);
    
    // 处理负数
    if (num < 0) {
        strbuf_write_char(sb, '-');
        num = -num;
    }
    
    if (num == 0) {
        strbuf_write_char(sb, '0');
        return;
    }
    
    while (num != 0) {
        c = num % 10;
        buf[count++] = c + '0';
        num /= 10;
    }
    
    // 反转字符串
    char tmp;
    for (int i = 0; i < count / 2; i++) {
        tmp = buf[i];
        buf[i] = buf[(count - 1) - i];
        buf[(count - 1) - i] = tmp;
    }
    
    strbuf_write(sb, buf, count);
}

// 向字符串缓冲区写入字符串
static void strbuf_write_string(strbuf_t *sb, const char *str) {
    if (!str) {
        strbuf_write(sb, "(null)", 6);
        return;
    }
    
    int count = 0;
    const char *p = str;
    while (*p++) count++;
    strbuf_write(sb, str, count);
}

// 向字符串缓冲区写入整数
void strbuf_write_int(strbuf_t *sb, int num) {
    strbuf_write_long(sb, (long)num);
}

// 计算10的n次方
static long long power_of_10(int n) {
    long long r = 1;
    for (int i = 0; i < n; i++) r *= 10;
    return r;
}

// 无符号整数转字符串
static int ulong_to_str(unsigned long n, char *buf) {
    int i = 0;
    char tmp[32];
    do {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    } while (n);
    int len = i;
    for (int j = 0; j < len; j++) {
        buf[j] = tmp[len - 1 - j];
    }
    return len;
}

// 向字符串缓冲区写入浮点数
static void strbuf_write_double(strbuf_t *sb, double d, int dec) {
    char buf[128];
    int len = 0;
    
    // 处理负数
    if (d < 0) {
        buf[len++] = '-';
        d = -d;
    }
    
    // 取整数部分和小数部分
    unsigned long integer = (unsigned long)d;
    double frac = d - integer;
    
    // 四舍五入
    long long frac_part = (long long)(frac * power_of_10(dec) + 0.5);
    if (frac_part >= power_of_10(dec)) {
        integer++;
        frac_part = 0;
    }
    
    // 输出整数部分
    len += ulong_to_str(integer, buf + len);
    
    // 输出小数点
    buf[len++] = '.';
    
    // 输出小数部分，补零到 dec 位
    char frac_buf[16];
    int frac_len = ulong_to_str(frac_part, frac_buf);
    for (int i = 0; i < dec - frac_len; i++) {
        buf[len++] = '0';
    }
    for (int i = 0; i < frac_len; i++) {
        buf[len++] = frac_buf[i];
    }
    
    strbuf_write(sb, buf, len);
}

// 核心函数：格式化输出到字符串缓冲区
static void vsnprintf_core(strbuf_t *sb, const char *fmt, my_va_list args) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            // 输出普通字符
            const char *start = p;
            while (*p && *p != '%') {
                p++;
            }
            strbuf_write(sb, start, p - start);
            if (!*p) break;
            p--; // 让循环的 p++ 跳过这个 %
            continue;
        }
        
        // 处理 % 格式
        p++;
        switch (*p) {
            case 'd': {
                long n = my_va_arg(args, long);
                strbuf_write_long(sb, n);
                break;
            }
            case 'l':
                if (*(p + 1) == 'd') {
                    p++;
                    long n = my_va_arg(args, long);
                    strbuf_write_long(sb, n);
                } else {
                    long n = my_va_arg(args, long);
                    strbuf_write_long(sb, n);
                }
                break;
            case 's': {
                char *s = my_va_arg(args, char*);
                strbuf_write_string(sb, s);
                break;
            }
            case 'c': {
                char c = (char)my_va_arg(args, int);
                strbuf_write_char(sb, c);
                break;
            }
            case 'f': {
                double d = my_va_arg(args, double);
                strbuf_write_double(sb, d, 6);
                break;
            }
            case '%': {
                strbuf_write_char(sb, '%');
                break;
            }
            default:
                strbuf_write_char(sb, '%');
                strbuf_write_char(sb, *p);
                break;
        }
    }
}

// snprintf 主函数
int snprintf(char *str, size_t size, const char *format, ...) {
    if (size == 0) {
        // size为0时，只计算所需长度，不写入数据
        strbuf_t sb = {NULL, 0, 0, 0};
        my_va_list args;
        my_va_start(args, format);
        vsnprintf_core(&sb, format, args);
        my_va_end(args);
        return (int)sb.pos;
    }
    
    strbuf_t sb = {str, size, 0, 0};
    my_va_list args;
    my_va_start(args, format);
    vsnprintf_core(&sb, format, args);
    my_va_end(args);
    
    // 添加字符串结束符
    if (sb.pos < size) {
        str[sb.pos] = '\0';
    } else {
        str[size - 1] = '\0';
    }
    
    // 返回实际需要的字符数（不包括结束符）
    return (int)sb.pos;
}