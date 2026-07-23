/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * toyc_rt.c — toyc 独立运行时
 *
 * 为 toyc/toypp/toyas 提供减少外部依赖的执行环境。
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
 * 注意：不包含 toyc.h，仅包含最小化头文件 toyc_need.h。
 * toyc_need.h 提供类型、常量、系统调用宏和函数声明。
 */

#include "toyc_need.h"

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
    return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
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
    size_t *base = (size_t *)ptr - 1;
    size_t total = *base + MALLOC_HDR_SZ;
    __munmap((void *)base, total);
}

/* ── 变参 __printf（使用 __builtin_va_*，编译器内置，不依赖 libc） ── */

/*
 * 支持的格式子集：%s %d %c %x %f %%
 */

/* 前向声明 */
static void print_dec(long n, int fd);

/* 辅助：输出浮点 double（最多 6 位小数）
 * 注意：参数顺序为 (fd, val) 以匹配 toyc 的 ABI：int 在前 → RDI，double 在后 → XMM0 */
static void print_double(int fd, double val)
{
    if (val < 0.0) {
        __write(fd, "-", 1);
        val = -val;
    }
    /* 整数部分 */
    long ipart = (long)val;
    double frac = val - (double)ipart;
    if (frac < 0.0) frac = -frac;
    print_dec(ipart, fd);
    __write(fd, ".", 1);
    /* 小数部分：逐位输出 */
    for (int i = 0; i < 6; i++) {
        frac *= 10.0;
        int d = (int)frac;
        char c = '0' + d;
        __write(fd, &c, 1);
        frac -= (double)d;
    }
}

/* 辅助：输出十进制整数 */
static void print_dec(long n, int fd)
{
    char buf[32];
    int i = 0;

    if (n < 0) {
        __write(fd, "-", 1);
        n = -n;
    }
    if (n == 0) {
        __write(fd, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0)
        __write(fd, &buf[--i], 1);
}

/* 辅助：输出十六进制整数 */
static void print_hex(unsigned long n, int fd)
{
    const char *hex = "0123456789abcdef";
    char buf[17];
    int i = 0;

    if (n == 0) {
        __write(fd, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = hex[n & 0xf];
        n >>= 4;
    }
    while (i > 0)
        __write(fd, &buf[--i], 1);
}

/* ── va_arg double 直接读取辅助 ──
 * 直接从 FP 寄存器保存区读取 double，绕过 __builtin_va_arg
 * 在自举编译中的 is_float 检测 bug。 */

/* va_list 布局（x86_64 SysV ABI）：4+4+8+8=24 字节 */
struct _tcc_valist {
    unsigned gp_offset;
    unsigned fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
};

static double _tcc_va_double(struct _tcc_valist *ap) {
    double *p;
    if (ap->fp_offset < 112) {  /* 48 + 8*8 = 112 */
        p = (double *)((char *)ap->reg_save_area + ap->fp_offset);
        ap->fp_offset += 8;
    } else {
        p = (double *)ap->overflow_arg_area;
        ap->overflow_arg_area = (char *)ap->overflow_arg_area + 8;
    }
    return *p;
}

/*
 * __printf(fmt, ...) — 变参格式化输出
 *
 * 使用编译器内置变参宏（__builtin_va_list / __builtin_va_start
 * / __builtin_va_arg / __builtin_va_end）获取变参，
 * 编译时无需包含 stdarg.h。
 *
 * 示例：
 *   __printf("hello\n")
 *   __printf("err: %s\n",  msg)
 *   __printf("%s (%d, %d)\n", s, x, y)
 */
void __printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            __write(1, p, 1);
            p++;
            continue;
        }
        p++;  /* 跳过 % */
        switch (*p) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            int slen = strlen(s);
            __write(1, s, slen);
            break;
        }
        case 'd':
        case 'i':
            print_dec(__builtin_va_arg(ap, long), 1);
            break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            __write(1, &c, 1);
            break;
        }
        case 'x':
        case 'X':
            print_hex((unsigned long)__builtin_va_arg(ap, long), 1);
            break;
        case 'f':
        case 'F':
            print_double(1, _tcc_va_double((struct _tcc_valist *)&ap));
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

    __builtin_va_end(ap);
}

/* __eprintf — 格式化输出到 stderr (fd 2)。
 * 注意：case 块内不声明局部变量（char buf[N] 等），全部委托给外部函数。
 * 否则 toyc 在变参函数 case 内的栈布局会出问题。 */
void __eprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            __write(2, p, 1);
            p++;
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            int slen = strlen(s);
            __write(2, s, slen);
            break;
        }
        case 'd':
        case 'i':
            print_dec(__builtin_va_arg(ap, long), 2);
            break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            __write(2, &c, 1);
            break;
        }
        case 'x':
        case 'X':
            print_hex((unsigned long)__builtin_va_arg(ap, long), 2);
            break;
        case 'f':
        case 'F':
            print_double(2, _tcc_va_double((struct _tcc_valist *)&ap));
            break;
        case '%':
            __write(2, "%", 1);
            break;
        default:
            __write(2, p, 1);
            break;
        }
        p++;
    }

    __builtin_va_end(ap);
}

