/* SPDX-License-Identifier: MIT
 *
 * signal.c — 信号处理相关 syscall 包装 + sigaction 辅助
 *
 * 约定：返回 Linux 内核风格负 errno（-ENOENT, -EINVAL 等）。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"
#include "string.h"        /* __memset */

/* ── 信号 syscall ── */

int __sigaction(int signum, const struct sigaction *act,
                struct sigaction *oldact)
{
    return syscall(SYS_rt_sigaction, signum, act, oldact, 8);
}

void rt_sig_restore(void)
{
    __asm__ volatile (
        "mov $15, %%rax\n\tsyscall\n\t"
        :
        :
        : "rax", "memory"
    );
}

int tlibc_sigaction(int signum, void (*handler)(int))
{
    struct sigaction sig;
    __memset(&sig, 0, sizeof(struct sigaction));

#define SA_RESTORER   0x04000000
    sig.sa_handler = handler;
    sig.sa_restorer = rt_sig_restore;
    sig.sa_flags = SA_RESTORER;
    unsigned long mask = 0;
    sig.sa_mask.sig[0] = mask;
    return __sigaction(signum, &sig, (void *)0);
}

int __rt_sigprocmask(int how, const sigset_t *set,
                     sigset_t *oldset, size_t sigsetsize)
{
    return syscall(SYS_rt_sigprocmask, how, set, oldset, sigsetsize);
}
