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

#define SYS_ioctl __NR_ioctl

// SC7自定义，在qemu才有效
#define SYS_shutdown 1000
