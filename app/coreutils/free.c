/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * free — 显示系统内存使用情况
 *
 * 机制：解析 /proc/meminfo 的 MemTotal/MemFree/MemAvailable/Buffers/Cached/
 *       Shmem/SReclaimable/SwapTotal/SwapFree，计算并格式化输出。
 *
 * 系统调用：openat, read, close
 *
 * 用法：
 *   free        # 默认人类可读格式
 *
 * 索引：
 *   main            解析 /proc/meminfo → 计算 → 输出
 */

#include "tlibc_everything.h"

static int opt_human = 1;  /* 默认人类可读 */

/* ── 从 key: value kB 格式中提取数值 ── */
static long extract_value(const char *buf, const char *key)
{
    const char *p = buf;
    int klen = strlen(key);
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == ':') {
            p += klen + 1;
            while (*p == ' ' || *p == '\t') p++;
            long v = 0;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
            }
            return v;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return -1;
}

/* ── 将 kB 转为人类可读字符串 ── */
static void format_size(long kb, char *buf, int size)
{
    if (!opt_human) {
        snprintf(buf, size, "%ld", kb);
        return;
    }

    const char *units[] = {"KiB", "MiB", "GiB", "TiB"};
    unsigned long long bytes = (unsigned long long)kb * 1024;
    unsigned long long divisor = 1024;  /* start from KiB */
    int u = 0;  /* 0=KiB */

    while (bytes / divisor >= 1024 && u < 3) {
        divisor *= 1024;
        u++;
    }

    /* val now in units[u] */
    unsigned long long val = (bytes + divisor / 2) / divisor;

    if (val >= 1000 || u == 0) {
        snprintf(buf, size, "%ld %s", (long)val, units[u]);
    } else if (val >= 10) {
        /* 一位小数 */
        unsigned long long scaled = (bytes + divisor / 20) / (divisor / 10);
        snprintf(buf, size, "%ld.%ld %s",
                 (long)(scaled / 10), (long)(scaled % 10), units[u]);
    } else {
        /* 两位小数 */
        unsigned long long scaled = (bytes + divisor / 200) / (divisor / 100);
        snprintf(buf, size, "%ld.%02ld %s",
                 (long)(scaled / 100), (long)(scaled % 100), units[u]);
    }
}

static void print_usage(void)
{
    printf("Usage: free [-h]\n");
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0)
            opt_human = 1;
    }

    int fd = openat(AT_FDCWD, "/proc/meminfo", O_RDONLY, 0);
    if (fd < 0) {
        printf("free: cannot open /proc/meminfo\n");
        return 1;
    }

    char buf[4096];
    __memset(buf, 0, sizeof(buf));
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        printf("free: cannot read /proc/meminfo\n");
        return 1;
    }
    buf[n] = '\0';

    long mem_total   = extract_value(buf, "MemTotal");
    long mem_free    = extract_value(buf, "MemFree");
    long mem_avail   = extract_value(buf, "MemAvailable");
    long buffers     = extract_value(buf, "Buffers");
    long cached      = extract_value(buf, "Cached");
    long s_reclaim   = extract_value(buf, "SReclaimable");
    long shmem       = extract_value(buf, "Shmem");
    long swap_total  = extract_value(buf, "SwapTotal");
    long swap_free   = extract_value(buf, "SwapFree");

    if (mem_total < 0) {
        printf("free: failed to parse /proc/meminfo\n");
        return 1;
    }

    /* 缺省值处理 */
    if (buffers < 0) buffers = 0;
    if (cached < 0) cached = 0;
    if (s_reclaim < 0) s_reclaim = 0;
    if (shmem < 0) shmem = 0;
    if (mem_free < 0) mem_free = 0;
    if (mem_avail < 0) mem_avail = mem_free;  /* 回退 */

    long mem_used = mem_total - mem_free - buffers - cached - s_reclaim;
    if (mem_used < 0) mem_used = 0;

    long buf_cache = buffers + cached + s_reclaim;

    long swap_used = (swap_total > 0 && swap_free >= 0) ? swap_total - swap_free : 0;
    if (swap_used < 0) swap_used = 0;

    /* 格式化 */
    char s_total[16], s_used[16], s_free[16], s_shared[16], s_buf_cache[16], s_avail[16];
    char s_sw_total[16], s_sw_used[16], s_sw_free[16];

    format_size(mem_total,   s_total,    sizeof(s_total));
    format_size(mem_used,    s_used,     sizeof(s_used));
    format_size(mem_free,    s_free,     sizeof(s_free));
    format_size(shmem,       s_shared,   sizeof(s_shared));
    format_size(buf_cache,   s_buf_cache,sizeof(s_buf_cache));
    format_size(mem_avail,   s_avail,    sizeof(s_avail));
    format_size(swap_total,  s_sw_total, sizeof(s_sw_total));
    format_size(swap_used,   s_sw_used,  sizeof(s_sw_used));
    format_size(swap_free,   s_sw_free,  sizeof(s_sw_free));

    /* 列宽：total(12) + used(11) + free(11) + shared(11) + buff/cache(13) + available(11) */
    printf("%-5s %12s %11s %11s %11s %13s %11s\n",
           "", "total", "used", "free", "shared", "buff/cache", "available");
    printf("%-5s %12s %11s %11s %11s %13s %11s\n",
           "Mem:",  s_total, s_used, s_free, s_shared, s_buf_cache, s_avail);
    printf("%-5s %12s %11s %11s %11s %13s %11s\n",
           "Swap:", s_sw_total, s_sw_used, s_sw_free, "", "", "");

    return 0;
}
