/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * toyc_need.h — toyc 独立项目的最小化头文件
 *
 * 从 Tinylibc 项目中提取 toyc 所需的类型、常量、函数声明和系统调用宏，
 * 避免直接依赖 tlibc_everything.h 和 arch/syscall.h，从而可以作为
 * 独立项目编译，不与原项目产生冲突。
 *
 * 包含：
 *   基础类型    uint64_t / size_t / off_t 等
 *   IO 常量      O_RDONLY / AT_FDCWD / SEEK_SET 等
 *   系统调用号   SYS_read / SYS_write / SYS_mmap 等
 *   系统调用宏   syscall(...) 自动派发到 __syscall0-6
 *   函数声明     __write / __printf / tlibc_malloc 等（实现在 tcc_rt.c）
 */

#ifndef TOYC_NEED_H
#define TOYC_NEED_H

/* ═══════════════════════════════════════════════════════════════
 *  基础类型 (from tlibc_types.h)
 * ═══════════════════════════════════════════════════════════════ */

typedef unsigned long           size_t;
typedef long                    ptrdiff_t;
typedef long                    off_t;
typedef unsigned int            mode_t;

typedef signed char             int8_t;
typedef unsigned char           uint8_t;
typedef signed short int        int16_t;
typedef unsigned short int      uint16_t;
typedef signed int              int32_t;
typedef unsigned int            uint32_t;
typedef signed long int         int64_t;
typedef unsigned long int       uint64_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ═══════════════════════════════════════════════════════════════
 *  IO / POSIX 常量
 * ═══════════════════════════════════════════════════════════════ */

#define AT_FDCWD    (-100)

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* ═══════════════════════════════════════════════════════════════
 *  内存映射常量
 * ═══════════════════════════════════════════════════════════════ */

#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_FAILED      ((void *)-1)

/* ═══════════════════════════════════════════════════════════════
 *  x86_64 Linux 系统调用号（toyc 使用的子集）
 * ═══════════════════════════════════════════════════════════════ */

#define SYS_read        0
#define SYS_write       1
#define SYS_close       3
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_exit        60
#define SYS_openat      257
#define SYS_brk         12

/* ═══════════════════════════════════════════════════════════════
 *  x86_64 系统调用内联汇编 (from arch/x86_64/syscall_arch.h)
 *
 *  使用 syscall 指令，参数 4 走 r10（不是 rcx，因为 syscall 会破坏 rcx）。
 *  通过 __SYSCALL_NARGS 计数宏自动派发到 __syscall0-6。
 * ═══════════════════════════════════════════════════════════════ */

static inline long __syscall0(long n)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall1(long n, long a1)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall2(long n, long a1, long a2)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
                          : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall3(long n, long a1, long a2, long a3)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
                          "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4)
        : "rcx", "r11", "r10", "memory");
    return ret;
}

static inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tmov %6, %%r8\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5)
        : "rcx", "r11", "r10", "r8", "memory");
    return ret;
}

static inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tmov %6, %%r8\n\tmov %7, %%r9\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5), "r"(a6)
        : "rcx", "r11", "r10", "r8", "r9", "memory");
    return ret;
}

/* ─── syscall 参数类型转换（指针→long，兼容 x86_64 ABI） ─── */

#define __scc(X) ((long)(X))

/* ─── 参数转换包装宏（与 inline 函数同名，通过宏展开调用函数） ── */

#define __syscall0(n)       __syscall0(n)
#define __syscall1(n, a)    __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define __syscall4(n, a, b, c, d) __syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))
#define __syscall5(n, a, b, c, d, e) __syscall5(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e))
#define __syscall6(n, a, b, c, d, e, f) __syscall6(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e), __scc(f))

/* ─── syscall 参数计数与派发宏 ─── */

#define __SYSCALL_NARGS_X(a,b,c,d,e,f,g,h,n,...) n
#define __SYSCALL_NARGS(...) \
    __SYSCALL_NARGS_X(__VA_ARGS__,7,6,5,4,3,2,1,0,)
#define __SYSCALL_CONCAT_X(a,b) a##b
#define __SYSCALL_CONCAT(a,b) __SYSCALL_CONCAT_X(a,b)
#define __SYSCALL_DISP(b,...) \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)

/*
 * 用法示例：
 *   syscall(SYS_write, fd, buf, len)        → __syscall4(...)
 *   syscall(SYS_exit, code)                 → __syscall2(...)
 *   syscall(SYS_mmap, addr, len, prot,...)  → __syscall6(...)
 */

/* ═══════════════════════════════════════════════════════════════
 *  toyc 运行时函数声明 (实现在 toyc_rt.c)
 * ═══════════════════════════════════════════════════════════════ */

/* Syscall 包装 */
extern long   __write(int fd, const void *buf, size_t len);
extern void   __exit(int code);
extern int    __openat(int dirfd, const char *path, int flags, int mode);
extern long   __read(int fd, void *buf, size_t count);
extern int    __close(int fd);
extern off_t  __lseek(int fd, off_t offset, int whence);
extern void  *__mmap(void *addr, size_t length, int prot, int flags,
                     int fd, off_t offset);
extern int    __munmap(void *addr, size_t length);

/*
 * __printf(fmt, ...) — 格式化输出。
 * 实现使用 __builtin_va_*（编译器内置，不需要 libc）。
 */
extern void   __printf(const char *fmt, ...);

/* __eprintf — 格式化输出到 stderr (fd 2)。实现同 __printf 但写 fd 2。 */
extern void   __eprintf(const char *fmt, ...);

/* 内存分配 */
extern void  *tlibc_malloc(size_t size);
extern void   tlibc_free(void *ptr);

/* 字符串函数 */
extern int    strcmp(const char *s1, const char *s2);
extern int    strlen(const char *s);
extern void  *__memset(void *dst, int val, size_t n);

#endif /* TOYC_NEED_H */
