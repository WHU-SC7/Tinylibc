#ifndef __TIME_H
#define __TIME_H

#include "tlibc_types.h"  /* time_t, clockid_t, size_t */

/* ================================================================
 *  Layer 1 — 核心：struct timespec / clock IDs（core.h 依赖）
 * ================================================================ */

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

/* clock_gettime / clock_nanosleep 时钟 ID */
#define CLOCK_REALTIME                  0
#define CLOCK_MONOTONIC                 1
#define CLOCK_PROCESS_CPUTIME_ID        2
#define CLOCK_THREAD_CPUTIME_ID         3
#define CLOCK_MONOTONIC_RAW             4
#define CLOCK_REALTIME_COARSE           5
#define CLOCK_MONOTONIC_COARSE          6
#define CLOCK_BOOTTIME                  7
#define CLOCK_REALTIME_ALARM            8
#define CLOCK_BOOTTIME_ALARM            9

/* clock_nanosleep 标志 */
#define TIMER_ABSTIME   1

/* ================================================================
 *  Layer 2 — POSIX：struct tm / timeval / 函数声明
 * ================================================================ */

struct tm {
    int tm_sec;    /* 秒   [0-60]   */
    int tm_min;    /* 分   [0-59]   */
    int tm_hour;   /* 时   [0-23]   */
    int tm_mday;   /* 日   [1-31]   */
    int tm_mon;    /* 月   [0-11]   */
    int tm_year;   /* 年   - 1900   */
    int tm_wday;   /* 周几 [0-6]    (0=Sunday) */
    int tm_yday;   /* 年日 [0-365]  */
    int tm_isdst;  /* 夏令时标志    (>0 DST, 0 非, <0 未知) */
};

struct timeval {
    time_t tv_sec;     /* 秒 */
    long   tv_usec;    /* 微秒 */
};

/* ── 函数声明 ── */

/* 将 time_t 分解为 struct tm（UTC） */
struct tm *gmtime_r(const time_t *timep, struct tm *result);

/* 将 time_t 分解为 struct tm（本地时区——当前等同 UTC） */
struct tm *localtime_r(const time_t *timep, struct tm *result);

/* 将 struct tm 转换为 time_t（视为本地时间） */
time_t mktime(struct tm *tm);

/* 格式化时间字符串 */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

/* 获取当前时间（微秒精度） */
int gettimeofday(struct timeval *tv, void *tz);

#endif /* __TIME_H */
