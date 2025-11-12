#define SYS_write 64
#define SYS_read 63
#define SYS_openat 56
#define SYS_close 57

#define SYS_getdents64 61
#define SYS_fstat 80
#define SYS_unlinkat 35
#define SYS_getcwd 17
#define SYS_chdir 49
#define SYS_mkdirat 34
#define SYS_renameat2 276

#define SYS_fork 300
#define SYS_exit 93
#define SYS_wait4 260
#define SYS_execve 221

// SC7自定义，在qemu才有效
#define SYS_shutdown 1000
