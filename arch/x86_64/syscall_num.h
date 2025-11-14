#include "syscall.h.in"

#define SYS_write       __NR_write
#define SYS_read        __NR_read
#define SYS_openat      __NR_openat
#define SYS_close       __NR_close

#define SYS_getdents64  __NR_getdents64
#define SYS_fstat       __NR_fstat
#define SYS_unlinkat    __NR_unlinkat
#define SYS_getcwd      __NR_getcwd
#define SYS_chdir       __NR_chdir
#define SYS_mkdirat     __NR_mkdirat
#define SYS_renameat2   __NR_renameat2

#define SYS_fork        __NR_fork
#define SYS_exit        __NR_exit
#define SYS_wait4       __NR_wait4
#define SYS_execve      __NR_execve

#define SYS_brk         __NR_brk
#define SYS_nanosleep   __NR_nanosleep
#define SYS_rt_sigaction   __NR_rt_sigaction
#define SYS_clone       __NR_clone
#define SYS_pipe2       __NR_pipe2
#define SYS_sched_yield       __NR_sched_yield
#define SYS_setsid      __NR_setsid
#define SYS_rt_sigprocmask  __NR_rt_sigprocmask
#define SYS_kill        __NR_kill
#define SYS_getpid      __NR_getpid
#define SYS_getrandom   __NR_getrandom

#define SYS_ioctl __NR_ioctl

#define SYS_lseek       __NR_lseek
#define SYS_ftruncate   __NR_ftruncate

#define SYS_time        __NR_time
#define SYS_clock_gettime       __NR_clock_gettime

#define SYS_readlinkat  __NR_readlinkat

// SC7自定义，在qemu才有效
#define SYS_shutdown 1000
