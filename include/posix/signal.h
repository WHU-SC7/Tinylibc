#ifndef __SIGNAL_H
#define __SIGNAL_H

#include "tlibc_types.h"  /* pid_t, uid_t */

/* ── 标准信号编号 ── */
#define SIGHUP       1
#define SIGINT       2
#define SIGQUIT      3
#define SIGILL       4
#define SIGTRAP      5
#define SIGABRT      6
#define SIGBUS       7
#define SIGFPE       8
#define SIGKILL      9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGXCPU     24
#define SIGXFSZ     25
#define SIGVTALRM   26
#define SIGPROF     27
#define SIGWINCH    28
#define SIGIO       29
#define SIGPWR      30
#define SIGSYS      31

/* sigprocmask 操作 */
#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

/* ── 信号集 ── */
#define _NSIG       64
#define _NSIG_BPW   64
#define _NSIG_WORDS (_NSIG / _NSIG_BPW)

typedef struct {
    unsigned long sig[_NSIG_WORDS];
} sigset_t;

/* ── siginfo_t ── */
typedef long clock_t;

typedef union sigval {
    int   sival_int;
    void *sival_ptr;
} sigval_t;

typedef struct {
    int      si_signo;
    int      si_errno;
    int      si_code;
    int      si_trapno;
    pid_t    si_pid;
    uid_t    si_uid;
    int      si_status;
    clock_t  si_utime;
    clock_t  si_stime;
    sigval_t si_value;
    int      si_int;
    void    *si_ptr;
    int      si_overrun;
    int      si_timerid;
    void    *si_addr;
    long     si_band;
    int      si_fd;
    short    si_addr_lsb;
    void    *si_lower;
    void    *si_upper;
    int      si_pkey;
    void    *si_call_addr;
    int      si_syscall;
    unsigned int si_arch;
} siginfo_t;

/* ── sigaction ── */
struct sigaction {
    void     (*sa_handler)(int);
    unsigned long sa_flags;
    void     (*sa_restorer)(void);
    sigset_t sa_mask;
};

#endif /* __SIGNAL_H */
