/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * tcc_rt.c — tcc 独立运行时
 *
 * 为 tcc/tpp/tas 提供减少外部依赖的执行环境。
 * 只依赖 arch/ 中的内联 syscall 宏，不依赖 tlibc.a。
 *
 * 包含：简化 _start → main → exit
 *       syscall 包装（__write/__openat/__read/__close/__lseek/__mmap/__munmap/__exit）
 *       字符串函数（strcmp/strlen/__memset）
 *       内存分配（tlibc_malloc/tlibc_free）
 *       固定参数格式化输出（__printf）
 */

/* ── 自包含的架构/类型头（仅宏+内联，无链接依赖） ── */

/*
 * 注意：不包含 tcc.h / tlibc_everything.h 等项目头文件。
 * tcc_rt.c 必须自给自足，才能独立链接。
 */

/* syscall 内联汇编：arch/syscall.h → arch/x86_64/syscall_arch.h */
#include "syscall.h"

/* x86_64 Linux syscall 号（自包含，不依赖 syscall.h.in 嵌套包含） */
#ifndef SYS_read
#define SYS_read        0
#define SYS_write       1
#define SYS_close       3
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_exit        60
#define SYS_openat      257
#endif

/* ── 基础类型 ── */

typedef unsigned long size_t;
typedef long off_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ── syscall 包装 ── */

long __write(int fd, const void *buf, size_t len)
{
    return syscall(SYS_write, fd, buf, len);
}

void __exit(int code)
{
    syscall(SYS_exit, code);
    for (;;)  /* unreachable */
        ;
}

int __openat(int dirfd, const char *path, int flags, int mode)
{
    return (int)syscall(SYS_openat, dirfd, path, flags, mode);
}

long __read(int fd, void *buf, size_t count)
{
    return syscall(SYS_read, fd, buf, count);
}

int __close(int fd)
{
    return (int)syscall(SYS_close, fd);
}

off_t __lseek(int fd, off_t offset, int whence)
{
    return syscall(SYS_lseek, fd, offset, whence);
}

void *__mmap(void *addr, size_t length, int prot, int flags,
             int fd, off_t offset)
{
#if defined(__GNUC__)
    /*
     * GCC/clang 版本：使用 syscall() 宏，正确处理 7 个参数。
     */
    return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
#else
    /*
     * TCC 版本：tcc 的函数调用只支持最多 6 个参数，
     * 而 __syscall6 需要 7 个参数（n + a1..a6），
     * 第 7 个参数会丢失。
     *
     * 通过定义与 cgen_asm 约定同名的局部变量
     * （n, a1..a6, ret），让 __asm__("syscall") 自动从
     * 局部变量表加载寄存器值。
     */
    long n    = SYS_mmap;
    long a1   = (long)addr;
    long a2   = (long)length;
    long a3   = (long)prot;
    long a4   = (long)flags;
    long a5   = (long)fd;
    long a6   = (long)offset;
    long ret;
    __asm__ __volatile__("syscall"
        : "=a"(ret) : "a"(n)
        : "rcx", "r11", "memory");
    return (void *)ret;
#endif
}

int __munmap(void *addr, size_t length)
{
    return (int)syscall(SYS_munmap, addr, length);
}

/* ── 字符串函数 ── */

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

void *__memset(void *dst, int val, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)val;
    return dst;
}

/* ── mmap 常值（自包含，不依赖系统头） ── */

#ifndef PROT_READ
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE     0x02
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS   0x20
#endif
#ifndef MAP_FAILED
#define MAP_FAILED      ((void *)-1)
#endif

/* ── 简易内存分配器（基于 mmap） ── */

#define MALLOC_HDR_SZ (sizeof(size_t))

void *tlibc_malloc(unsigned long size)
{
    if (size == 0) size = 1;
    void *addr = __mmap(0, size + MALLOC_HDR_SZ,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED)
        return NULL;
    *(size_t *)addr = size;
    void *user = (void *)((char *)addr + MALLOC_HDR_SZ);
    __memset(user, 0, size);
    return user;
}

void tlibc_free(void *ptr)
{
    if (!ptr) return;
    /* 跳过内核空间地址（高 16 位为 0xFFFF） */
    if (((unsigned long)ptr >> 48) == 0xFFFFUL)
        return;
    size_t *base = (size_t *)ptr - 1;
    size_t total = *base + MALLOC_HDR_SZ;
    __munmap((void *)base, total);
}

/* ── 固定参数 __printf ── */

/*
 * 不使用 __builtin_va_*，而是声明固定参数的函数签名。
 * x86_64 ABI 中 args 2–4 通过 rsi/rdx/rcx 传递；
 * 调用方传了 1 个还是 3 个参数，未写入的寄存器只是垃圾值，不会影响 callee。
 *
 * tcc 所有 __printf 调用最多 3 个格式参数（全局统计确认）。
 *
 * 支持的格式子集：%s %d %c %x %%
 */

/* 辅助：输出十进制整数 */
static void print_dec(long n)
{
    char buf[32];
    int i = 0;

    if (n < 0) {
        __write(1, "-", 1);
        n = -n;
    }
    if (n == 0) {
        __write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0)
        __write(1, &buf[--i], 1);
}

/* 辅助：输出十六进制整数 */
static void print_hex(unsigned long n)
{
    const char *hex = "0123456789abcdef";
    char buf[17];
    int i = 0;

    if (n == 0) {
        __write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = hex[n & 0xf];
        n >>= 4;
    }
    while (i > 0)
        __write(1, &buf[--i], 1);
}

/*
 * __printf(fmt, a1, a2, a3) — 固定 3 个格式参数
 *
 * 内部按格式串的 % 顺序消费参数。调用方传参少于 3 个时，
 * 未使用的寄存器参数不会被读取（格式串中不出现对应 %）。
 *
 * 示例：
 *   __printf("hello\n")                    → 不读 a1/a2/a3
 *   __printf("err: %s\n",  msg)            → 读 a1
 *   __printf("%s (%d, %d)\n", s, x, y)     → 读 a1/a2/a3
 */
void __printf(const char *fmt, long a1, long a2, long a3)
{
    /*
     * 用数组固定参数索引。格式串中的第 N 个 % 说明符消费 args[N]。
     * 不读取超出格式串实际需要的参数，所以"未设置的寄存器"安全。
     */
    long args[3] = { a1, a2, a3 };
    const char *p = fmt;
    int ai = 0;

    while (*p) {
        if (*p != '%') {
            __write(1, p, 1);
            p++;
            continue;
        }
        p++;  /* 跳过 % */
        switch (*p) {
        case 's': {
            const char *s = (const char *)args[ai++];
            int slen = strlen(s);
            __write(1, s, slen);
            break;
        }
        case 'd':
        case 'i':
            print_dec(args[ai++]);
            break;
        case 'c': {
            char c = (char)args[ai++];
            __write(1, &c, 1);
            break;
        }
        case 'x':
        case 'X':
            print_hex((unsigned long)args[ai++]);
            break;
        case '%':
            __write(1, "%", 1);
            break;
        default:
            __write(1, p, 1);
            break;
        }
        p++;
    }
}
