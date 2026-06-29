/* SPDX-License-Identifier: MIT
 *
 * time.c — POSIX 时间/日期格式化函数
 *
 * 实现：localtime_r / gmtime_r / mktime / strftime / gettimeofday
 * gmtime_r/localtime_r：注意 localtime_r 暂不处理时区（等同于 UTC）。
 */

#include "core.h"
#include "posix/time.h"
#include "syscall.h"
#include "syscall_num.h"

/* ================================================================
 *  内部辅助
 * ================================================================ */

/** 闰年判断 */
static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/** 每月天数表 [闰年标志][月份] */
static const int month_days[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

/** 星期名缩写 */
static const char *wday_abbr[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/** 星期名全称 */
static const char *wday_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

/** 月份名缩写 */
static const char *mon_abbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/** 月份名全称 */
static const char *mon_full[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* ================================================================
 *  gmtime_r — 将 time_t（UTC）分解为 struct tm
 * ================================================================ */

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    if (!result || !timep)
        return NULL;

    long long t = (long long)*timep;
    int     sec_of_day;

    /* 当天内秒数（处理负值） */
    sec_of_day = (int)(t % 86400);
    if (sec_of_day < 0) {
        sec_of_day += 86400;
        t -= 86400;
    }

    result->tm_sec  = sec_of_day % 60;
    result->tm_min  = (sec_of_day / 60) % 60;
    result->tm_hour = sec_of_day / 3600;

    /* 1970-01-01 是星期四 → wday = 4 */
    long long days = t / 86400;
    result->tm_wday = (int)((days + 4) % 7);
    if (result->tm_wday < 0)
        result->tm_wday += 7;

    /* 逐年减，确定年份 */
    int year = 1970;
    while (1) {
        int leap = is_leap(year);
        int ydays = leap ? 366 : 365;
        if (days < ydays)
            break;
        days -= ydays;
        year++;
    }

    result->tm_year  = year - 1900;
    result->tm_yday  = (int)days;

    /* 逐月减，确定月份 */
    int leap = is_leap(year);
    int mon;
    for (mon = 0; mon < 12; mon++) {
        int md = month_days[leap][mon];
        if (days < md)
            break;
        days -= md;
    }

    result->tm_mon   = mon;
    result->tm_mday  = (int)days + 1;
    result->tm_isdst = 0;

    return result;
}

/* ================================================================
 *  localtime_r — 本地时间分解
 *
 *  暂不处理时区 / 夏令时，等同 gmtime_r。
 *  待后续扩展：通过 TZ 环境变量 / /etc/localtime 获取时区偏移。
 * ================================================================ */

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return gmtime_r(timep, result);
}

/* ================================================================
 *  mktime — struct tm → time_t
 *
 *  视为本地时间（当前实现等同 UTC），同时修正 tm_wday / tm_yday。
 * ================================================================ */

time_t mktime(struct tm *tm)
{
    if (!tm)
        return (time_t)-1;

    int year = tm->tm_year + 1900;
    int mon  = tm->tm_mon;
    int day  = tm->tm_mday;
    int hour = tm->tm_hour;
    int min  = tm->tm_min;
    int sec  = tm->tm_sec;

    /* 累积 1970 到 year-1 的总天数 */
    long long days = 0;
    for (int y = 1970; y < year; y++)
        days += is_leap(y) ? 366 : 365;

    /* 当前年月内天数 */
    int leap = is_leap(year);
    for (int m = 0; m < mon; m++)
        days += month_days[leap][m];

    days += day - 1;

    /* 回填 wday */
    tm->tm_wday = (int)((days + 4) % 7);
    if (tm->tm_wday < 0)
        tm->tm_wday += 7;

    /* 回填 yday */
    tm->tm_yday = 0;
    for (int m = 0; m < mon; m++)
        tm->tm_yday += month_days[leap][m];
    tm->tm_yday += day - 1;

    tm->tm_isdst = 0;

    return (time_t)(days * 86400 + hour * 3600 + min * 60 + sec);
}

/* ================================================================
 *  strftime — 格式化时间字符串
 *
 *  支持的格式符：
 *    %% → %      %Y → 4 位年   %y → 2 位年
 *    %m → 月 01–12              %d → 日 01–31
 *    %H → 时 00–23              %I → 时 01–12
 *    %M → 分 00–59              %S → 秒 00–60
 *    %a → 星期名缩             %A → 星期名全
 *    %b → 月份名缩             %B → 月份名全
 *    %c → 日期时间             %p → AM/PM
 *    %j → 年日 001–366         %w → 星期 0–6（0=Sun）
 *    %x → 日期                  %X → 时间
 *    %z → 时区偏移（当前输出空串）
 * ================================================================ */

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    if (!s || !format || !tm || max == 0)
        return 0;

    /* snprintf 声明（来自 stdio/snprintf.c） */
    extern int snprintf(char *str, size_t size, const char *fmt, ...);

    size_t pos = 0;

    for (const char *p = format; *p && pos < max; p++) {
        if (*p != '%') {
            s[pos++] = *p;
            continue;
        }

        p++;  /* 跳过 % */
        if (!*p)
            break;

        /* 每个格式符最多写入 64 字节，确保不会溢出 */
        char buf[64];
        int  n;

        switch (*p) {
        case '%':
            buf[0] = '%';
            buf[1] = '\0';
            n = 1;
            break;
        case 'Y':
            n = snprintf(buf, sizeof(buf), "%04d", tm->tm_year + 1900);
            break;
        case 'y':
            n = snprintf(buf, sizeof(buf), "%02d", (tm->tm_year + 1900) % 100);
            break;
        case 'm':
            n = snprintf(buf, sizeof(buf), "%02d", tm->tm_mon + 1);
            break;
        case 'd':
            n = snprintf(buf, sizeof(buf), "%02d", tm->tm_mday);
            break;
        case 'H':
            n = snprintf(buf, sizeof(buf), "%02d", tm->tm_hour);
            break;
        case 'I': {
            int h12 = tm->tm_hour % 12;
            if (h12 == 0) h12 = 12;
            n = snprintf(buf, sizeof(buf), "%02d", h12);
            break;
        }
        case 'M':
            n = snprintf(buf, sizeof(buf), "%02d", tm->tm_min);
            break;
        case 'S':
            n = snprintf(buf, sizeof(buf), "%02d", tm->tm_sec);
            break;
        case 'a':
            n = snprintf(buf, sizeof(buf), "%s",
                         wday_abbr[tm->tm_wday % 7]);
            break;
        case 'A':
            n = snprintf(buf, sizeof(buf), "%s",
                         wday_full[tm->tm_wday % 7]);
            break;
        case 'b':
            n = snprintf(buf, sizeof(buf), "%s",
                         mon_abbr[tm->tm_mon % 12]);
            break;
        case 'B':
            n = snprintf(buf, sizeof(buf), "%s",
                         mon_full[tm->tm_mon % 12]);
            break;
        case 'p':
            n = snprintf(buf, sizeof(buf), "%s",
                         tm->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'j':
            n = snprintf(buf, sizeof(buf), "%03d", tm->tm_yday + 1);
            break;
        case 'w':
            n = snprintf(buf, sizeof(buf), "%d", tm->tm_wday);
            break;
        case 'c':
            /* 等效 "%a %b %e %H:%M:%S %Y" */
            n = snprintf(buf, sizeof(buf), "%s %s %02d %02d:%02d:%02d %04d",
                         wday_abbr[tm->tm_wday % 7],
                         mon_abbr[tm->tm_mon % 12],
                         tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec,
                         tm->tm_year + 1900);
            break;
        case 'x':
            n = snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                         tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
            break;
        case 'X':
            n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
            break;
        case 'z':
            /* 时区偏移 — 当前始终输出空串 */
            buf[0] = '\0';
            n = 0;
            break;
        default:
            /* 不识别的格式符：原样输出 */
            buf[0] = '%';
            buf[1] = *p;
            buf[2] = '\0';
            n = 2;
            break;
        }

        if (n < 0)
            return 0;

        if (pos + (size_t)n >= max) {
            /* 输出空间不足，截断 */
            size_t rem = max - pos - 1;
            for (size_t i = 0; i < rem; i++)
                s[pos + i] = buf[i];
            pos += rem;
            s[pos] = '\0';
            return pos;
        }

        for (size_t i = 0; i < (size_t)n; i++)
            s[pos + i] = buf[i];
        pos += (size_t)n;
    }

    /* 确保 null 终止 */
    if (pos < max)
        s[pos] = '\0';
    else if (max > 0)
        s[max - 1] = '\0';

    return pos;
}

/* ================================================================
 *  gettimeofday — 获取时间（微秒精度）
 * ================================================================ */

int gettimeofday(struct timeval *tv, void *tz)
{
    long ret = syscall(SYS_gettimeofday, tv, tz);
    if (ret < 0) {
        return -1;
    }
    return 0;
}
