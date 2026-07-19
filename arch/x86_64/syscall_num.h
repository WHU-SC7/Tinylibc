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
#define SYS_clock_nanosleep     __NR_clock_nanosleep
#define SYS_gettimeofday        __NR_gettimeofday

#define SYS_readlinkat  __NR_readlinkat

#define SYS_mmap        __NR_mmap
#define SYS_munmap      __NR_munmap
#define SYS_gettid      __NR_gettid
#define SYS_futex       __NR_futex
#define SYS_arch_prctl  __NR_arch_prctl

//网络
#define SYS_socket      __NR_socket
#define SYS_connect     __NR_connect
#define SYS_setsockopt  __NR_setsockopt
#define SYS_bind        __NR_bind
#define SYS_listen      __NR_listen
#define SYS_accept      __NR_accept
#define SYS_recvfrom    __NR_recvfrom
#define SYS_sendto     __NR_sendto

#define SYS_fcntl       __NR_fcntl
#define SYS_dup         __NR_dup
#define SYS_dup2        __NR_dup2
#define SYS_dup3        __NR_dup3

#define SYS_getuid       __NR_getuid

#define SYS_chmod       __NR_chmod
#define SYS_stat        __NR_stat
#define SYS_statfs      __NR_statfs
#define SYS_fstatfs     __NR_fstatfs

#define SYS_shutdown    __NR_shutdown

#define SYS_getsockname __NR_getsockname

#define SYS_poll        __NR_poll
#define SYS_ppoll       __NR_ppoll
#define SYS_pselect6    __NR_pselect6

#define SYS_epoll_create1   __NR_epoll_create1
#define SYS_epoll_ctl       __NR_epoll_ctl
#define SYS_epoll_wait      __NR_epoll_wait
#define SYS_epoll_pwait     __NR_epoll_pwait
