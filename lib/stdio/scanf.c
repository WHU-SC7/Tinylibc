/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * scanf / sscanf / vsscanf — 格式化输入解析
 *
 * 机制：逐个字符比对格式字符串与输入字符串，支持 %d %i %u %x %X %s %c %n %%
 * 和宽度限定（%Ns）、赋值抑制（%*d）、长度修饰（%ld %lld）。除 %c、%n、%%
 * 外所有 % 格式自动跳过前导空白。
 *
 * 系统调用：read（scanf 从 stdin 读取一行）
 *
 * 用法：
 *   sscanf(str, "%d %d", &a, &b)          // 从字符串解析
 *   scanf("%d %d", &a, &b)                // 从 stdin 读取一行后解析
 *   sscanf(cmd, "color %x", &color)       // 十六进制解析
 *   sscanf(cmd, "text %d %d %63s", &x, &y, str)  // 带宽度字符串
 *
 * 索引：
 *   vsscanf           核心实现：遍历格式串 → 匹配输入
 *     skip_ws         跳过输入空白
 *     parse %d        有符号十进制
 *     parse %i        自动检测进制（0x→16, 0→8, 其他→10）
 *     parse %u        无符号十进制
 *     parse %x/%X     十六进制
 *     parse %s        空白分隔字符串（自动 null 终止）
 *     parse %c        字符（不跳过空白，可带宽度）
 *     parse %n        存储已消费字符数
 *   sscanf            变参 → vsscanf
 *   scanf             read stdin → sscanf
 */

#include "core.h"
#include "tlibc_everything.h"
#include "ctype.h"

typedef __builtin_va_list my_va_list;
#define my_va_start(v, l)   __builtin_va_start(v, l)
#define my_va_arg(v, t)     __builtin_va_arg(v, t)
#define my_va_end(v)        __builtin_va_end(v)

/* 跳过输入字符串中的空白字符 */
static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

/* ================================================================== */
/*  vsscanf 核心实现                                                   */
/* ================================================================== */

int vsscanf(const char *str, const char *fmt, my_va_list args)
{
    if (!str || !fmt) return -1;
    if (!*str) return -1;   /* EOF */

    const char *s = str;
    int assignments = 0;

    while (*fmt) {
        /* 格式串空白 → 跳过输入空白 */
        if (isspace((unsigned char)*fmt)) {
            while (*fmt && isspace((unsigned char)*fmt))
                fmt++;
            s = skip_ws(s);
            continue;
        }

        /* 非 % 字符 → 精确匹配 */
        if (*fmt != '%') {
            if (*s != *fmt) break;
            s++; fmt++;
            continue;
        }

        /* ── 解析 % 说明符 ── */
        fmt++;  /* 跳过 '%' */

        /* %% → 匹配字面 % */
        if (*fmt == '%') {
            if (*s == '%') { s++; fmt++; continue; }
            break;
        }

        /* 赋值抑制 * */
        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }

        /* 宽度 */
        int width = 0;
        int has_width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            has_width = 1;
            fmt++;
        }

        /* 长度修饰 l / ll */
        int long_cnt = 0;
        while (*fmt == 'l') { if (long_cnt < 2) long_cnt++; fmt++; }

        char spec = *fmt;
        if (!spec) break;
        fmt++;

        switch (spec) {
            case 'd': {
                /* 有符号十进制 */
                s = skip_ws(s);
                if (!*s) goto done;
                int sign = 1;
                if      (*s == '-') { sign = -1; s++; }
                else if (*s == '+') s++;
                if (!isdigit((unsigned char)*s)) break;
                unsigned long val = 0;
                int cnt = 0;
                while (*s && isdigit((unsigned char)*s) &&
                       (!has_width || cnt < width)) {
                    val = val * 10 + (*s - '0');
                    s++; cnt++;
                }
                if (!suppress) {
                    long result = (long)val * sign;
                    if (long_cnt >= 2)
                        *my_va_arg(args, long long *) = (long long)result;
                    else if (long_cnt == 1)
                        *my_va_arg(args, long *) = result;
                    else
                        *my_va_arg(args, int *) = (int)result;
                    assignments++;
                }
                break;
            }

            case 'i': {
                /* 自动检测进制：0x=16 进制, 0=8 进制, 其他=10 进制 */
                s = skip_ws(s);
                if (!*s) goto done;
                int sign = 1;
                if      (*s == '-') { sign = -1; s++; }
                else if (*s == '+') s++;
                if (!*s) break;

                unsigned long val = 0;
                int cnt = 0;
                int max = has_width ? width : 999999999;

                if (*s == '0') {
                    s++;
                    if ((*s == 'x' || *s == 'X') && isxdigit((unsigned char)*(s+1))) {
                        /* 16 进制 */
                        s++;
                        while (*s && isxdigit((unsigned char)*s) && cnt < max) {
                            char c = *s;
                            if      (c >= '0' && c <= '9')
                                val = val * 16 + (c - '0');
                            else if (c >= 'a' && c <= 'f')
                                val = val * 16 + (c - 'a' + 10);
                            else
                                val = val * 16 + (c - 'A' + 10);
                            s++; cnt++;
                        }
                    } else if (*s >= '0' && *s <= '7') {
                        /* 8 进制 */
                        while (*s && *s >= '0' && *s <= '7' && cnt < max) {
                            val = val * 8 + (*s - '0');
                            s++; cnt++;
                        }
                    } else {
                        /* 只有 '0' */
                        cnt = 1;
                    }
                } else if (isdigit((unsigned char)*s)) {
                    /* 10 进制 */
                    while (*s && isdigit((unsigned char)*s) && cnt < max) {
                        val = val * 10 + (*s - '0');
                        s++; cnt++;
                    }
                } else {
                    break;
                }

                if (!suppress) {
                    long result = (long)val * sign;
                    if (long_cnt >= 2)
                        *my_va_arg(args, long long *) = (long long)result;
                    else if (long_cnt == 1)
                        *my_va_arg(args, long *) = result;
                    else
                        *my_va_arg(args, int *) = (int)result;
                    assignments++;
                }
                break;
            }

            case 'u': {
                /* 无符号十进制 */
                s = skip_ws(s);
                if (!*s) goto done;
                if (*s == '+') s++;
                if (!isdigit((unsigned char)*s)) break;
                unsigned long val = 0;
                int cnt = 0;
                while (*s && isdigit((unsigned char)*s) &&
                       (!has_width || cnt < width)) {
                    val = val * 10 + (*s - '0');
                    s++; cnt++;
                }
                if (!suppress) {
                    if (long_cnt >= 2)
                        *my_va_arg(args, unsigned long long *) =
                            (unsigned long long)val;
                    else if (long_cnt == 1)
                        *my_va_arg(args, unsigned long *) = val;
                    else
                        *my_va_arg(args, unsigned int *) = (unsigned int)val;
                    assignments++;
                }
                break;
            }

            case 'x':
            case 'X': {
                /* 十六进制 */
                s = skip_ws(s);
                if (!*s) goto done;
                /* 跳过可选的 0x/0X 前缀 */
                if (*s == '0' && (*(s+1) == 'x' || *(s+1) == 'X'))
                    s += 2;
                if (!isxdigit((unsigned char)*s)) break;
                unsigned long val = 0;
                int cnt = 0;
                while (*s && isxdigit((unsigned char)*s) &&
                       (!has_width || cnt < width)) {
                    char c = *s;
                    if      (c >= '0' && c <= '9')
                        val = val * 16 + (c - '0');
                    else if (c >= 'a' && c <= 'f')
                        val = val * 16 + (c - 'a' + 10);
                    else
                        val = val * 16 + (c - 'A' + 10);
                    s++; cnt++;
                }
                if (!suppress) {
                    if (long_cnt >= 2)
                        *my_va_arg(args, unsigned long long *) =
                            (unsigned long long)val;
                    else if (long_cnt == 1)
                        *my_va_arg(args, unsigned long *) = val;
                    else
                        *my_va_arg(args, unsigned int *) = (unsigned int)val;
                    assignments++;
                }
                break;
            }

            case 's': {
                /* 字符串（空白分隔 → 自动 null 终止） */
                s = skip_ws(s);
                if (!*s) goto done;
                char *dest = NULL;
                if (!suppress)
                    dest = my_va_arg(args, char *);
                int cnt = 0;
                int max = has_width ? width : 999999999;
                while (*s && !isspace((unsigned char)*s) && cnt < max) {
                    if (dest) dest[cnt] = *s;
                    s++; cnt++;
                }
                if (dest) dest[cnt] = '\0';
                if (!suppress) assignments++;
                break;
            }

            case 'c': {
                /* 字符（不跳过空白，默认宽度 = 1，不 null 终止） */
                if (!*s) goto done;
                int max = has_width ? width : 1;
                char *dest = NULL;
                if (!suppress)
                    dest = my_va_arg(args, char *);
                int cnt = 0;
                while (*s && cnt < max) {
                    if (dest) dest[cnt] = *s;
                    s++; cnt++;
                }
                if (cnt > 0 && !suppress)
                    assignments++;
                break;
            }

            case 'n': {
                /* 不消费输入，存储已消费字符数（不计入返回值） */
                if (!suppress) {
                    int consumed = (int)(s - str);
                    if (long_cnt >= 2)
                        *my_va_arg(args, long long *) = (long long)consumed;
                    else if (long_cnt == 1)
                        *my_va_arg(args, long *) = (long)consumed;
                    else
                        *my_va_arg(args, int *) = consumed;
                }
                break;
            }
        }
    }

done:
    return assignments;
}

/* ================================================================== */
/*  sscanf — 从字符串解析                                              */
/* ================================================================== */

int sscanf(const char *str, const char *fmt, ...)
{
    if (!str) return -1;

    my_va_list args;
    my_va_start(args, fmt);
    int ret = vsscanf(str, fmt, args);
    my_va_end(args);
    return ret;
}

/* ================================================================== */
/*  scanf — 从 stdin 读取一行后解析                                    */
/* ================================================================== */

int scanf(const char *fmt, ...)
{
    char buf[4096];
    buf[0] = '\0';
    int n = __read(0, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* 去掉末尾换行/回车，方便格式串匹配 */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
        buf[--n] = '\0';

    my_va_list args;
    my_va_start(args, fmt);
    int ret = vsscanf(buf, fmt, args);
    my_va_end(args);
    return ret;
}
