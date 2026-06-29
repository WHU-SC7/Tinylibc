/* SPDX-License-Identifier: MIT
 *
 * proc.c — 进程控制相关 syscall 包装
 *
 * 约定：返回 Linux 内核风格负 errno（-ENOENT, -EINVAL 等）。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"

/* ── 进程生命周期 ── */

pid_t __fork()
{
    return syscall(SYS_fork);
}

void __exit(int status)
{
    syscall(SYS_exit, status);
}

void __exit_group(int status)
{
    syscall(__NR_exit_group, status);
}

// NOTE: only wait4 is the raw syscall (nr 260). wait/waitpid are wrappers.
// sys_wait4(pid_t pid, int __user *stat_addr, int options, struct rusage __user *ru)
pid_t __waitpid(int pid, int *wstatus, int options)
{
    return syscall(SYS_wait4, pid, wstatus, options, 0);
}

pid_t __wait(int *wstatus)
{
    return syscall(SYS_wait4, -1, wstatus, 0, 0);
}

int __execve(const char *pathname, char *const argv[], char *const envp[])
{
    return syscall(SYS_execve, pathname, argv, envp);
}

/* ── 进程标识 ── */

pid_t __gettid(void)
{
    return (pid_t)syscall(SYS_gettid);
}

pid_t __getpid(void)
{
    return syscall(SYS_getpid);
}

uid_t tlibc_getuid(void)
{
    return syscall(SYS_getuid);
}

pid_t __setsid(void)
{
    return syscall(SYS_setsid);
}

/* ── 进程控制 ── */

int __yield()
{
    return syscall(SYS_sched_yield);
}

int __kill(pid_t pid, int sig)
{
    return syscall(SYS_kill, pid, sig);
}
