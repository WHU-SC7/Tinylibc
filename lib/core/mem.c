/* SPDX-License-Identifier: MIT
 *
 * mem.c — 内存管理相关 syscall 包装 + malloc/free
 *
 * 约定：__ 函数返回 Linux 内核风格负 errno（-ENOENT, -EINVAL 等）。
 *       tlibc_malloc/tlibc_free 返回 NULL 表示失败。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"
#include "string.h"   /* __memset */

/* ── 内存映射 syscall ── */

long __brk(void *addr)
{
    return syscall(SYS_brk, addr);
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

int __madvise(void *addr, size_t length, int advice)
{
    return syscall(__NR_madvise, addr, length, advice);
}

/* ── malloc / free（基于 mmap 的简易分配器） ── */

/* 内存分配头：每个 mmap 区域头部存用户请求大小 */
#define MALLOC_HDR_SZ (sizeof(size_t))

void *tlibc_malloc(unsigned long size)
{
    if (size == 0) size = 1;
    void *addr = __mmap(0, size + MALLOC_HDR_SZ,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (addr == MAP_FAILED)
        return NULL;
    *(size_t *)addr = size;                       /* 存用户请求大小 */
    void *user = (void *)((char *)addr + MALLOC_HDR_SZ);
    __memset(user, 0, size);
    return user;
}

void tlibc_free(void *ptr)
{
    if (!ptr) return;
    /* 哨兵值（MAP_FAILED 即 (void*)-1、(void*)-2 等）位
     * 于内核地址空间（高 16 位为 0xFFFF），跳过以防误解引用。 */
    if (((unsigned long)ptr >> 48) == 0xFFFFUL) return;
    size_t *base = (size_t *)ptr - 1;
    size_t total = *base + MALLOC_HDR_SZ;
    __munmap((void *)base, total);
}
