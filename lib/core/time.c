/* SPDX-License-Identifier: MIT
 *
 * time.c — 时间相关 syscall 包装 + 实用睡眠函数
 *
 * 约定：返回 Linux 内核风格负 errno（-ENOENT, -EINVAL 等）。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"

/* ── 时钟 / 时间 syscall ── */

int __nanosleep(const struct timespec *req, struct timespec *rem)
{
    return syscall(SYS_nanosleep, req, rem);
}

time_t __time(time_t *tloc)
{
    return syscall(SYS_time, tloc);
}

int __clock_gettime(clockid_t clockid, struct timespec *tp)
{
    return syscall(SYS_clock_gettime, clockid, tp);
}

int __clock_nanosleep(clockid_t clockid, int flags,
                      const struct timespec *request, struct timespec *remain)
{
    return syscall(SYS_clock_nanosleep, clockid, flags, request, remain);
}

/* ── 实用睡眠函数 ── */

int tlibc_msleep(unsigned int msecond)
{
    struct timespec time;
    if (msecond < 1000) {
        time.tv_sec = 0;
        time.tv_nsec = msecond * 1000000;
        __nanosleep(&time, &time);
    } else {
        time.tv_sec = msecond / 1000;
        time.tv_nsec = (msecond % 1000) * 1000000;
        __nanosleep(&time, &time);
    }
    return 0;
}

int tlibc_usleep(unsigned int usecond)
{
    struct timespec time;
    if (usecond < 1000) {
        time.tv_sec = 0;
        time.tv_nsec = usecond * 1000;
        __nanosleep(&time, &time);
    } else {
        time.tv_sec = usecond / 1000000;
        time.tv_nsec = (usecond % 1000000) * 1000;
        __nanosleep(&time, &time);
    }
    return 0;
}

int tlibc_timespec_get(struct timespec *ts, int base)
{
    (void)base;
    return __clock_gettime(CLOCK_REALTIME, ts);
}
