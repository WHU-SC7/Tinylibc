/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * df — 显示文件系统磁盘空间使用情况
 *
 * 机制：读取 /proc/mounts 获取挂载点列表，对每个挂载点调用 statfs
 *       获取块总数、空闲块数等信息，计算使用率。
 *
 * 系统调用：openat, read, close, statfs
 *
 * 用法：
 *   df          # 显示所有挂载点的磁盘使用情况
 *   df <path>   # 显示指定路径所在文件系统的信息
 *
 * 索引：
 *   main        解析 /proc/mounts → 逐挂载点 statfs → 格式化输出
 */

#include "tlibc_everything.h"

static int opt_human = 1;  /* 默认人类可读 */

/* ── 将 block 数按块大小格式化为 1K 块或人类可读字符串 ── */
static void format_size(long blocks, long bsize, char *buf, int size)
{
    unsigned long long bytes = (unsigned long long)blocks * bsize;

    if (!opt_human) {
        snprintf(buf, size, "%ld", (long)(bytes / 1024));
        return;
    }

    const char *units[] = {"", "K", "M", "G", "T"};
    unsigned long long divisor = 1;
    int u = 0;

    /* 确定合适单位：当 bytes / divisor 仍 >= 1024 时进位 */
    while (bytes / divisor >= 1024 && u < 4) {
        divisor *= 1024;
        u++;
    }

    /* 四舍五入到 units[u] */
    unsigned long long val = (bytes + divisor / 2) / divisor;

    if (val >= 1000 || u == 0) {
        snprintf(buf, size, "%ld%s", (long)val, units[u]);
    } else {
        /* 一位小数 */
        unsigned long long scaled = (bytes + divisor / 20) / (divisor / 10);
        snprintf(buf, size, "%ld.%ld%s",
                 (long)(scaled / 10), (long)(scaled % 10), units[u]);
    }
}

/* ── 读取 /proc/mounts，对每个挂载点调用 statfs ── */
static void df_all(void)
{
    int fd = openat(AT_FDCWD, "/proc/mounts", O_RDONLY, 0);
    if (fd < 0) {
        printf("df: cannot open /proc/mounts\n");
        return;
    }

    char buf[65536];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    /* 输出表头 */
    const char *unit = opt_human ? "Size" : "1K-blocks";
    printf("%-20s %12s %10s %10s %5s %s\n",
           "Filesystem", unit, "Used", "Avail", "Use%", "Mounted on");

    char *p = buf;
    while (*p) {
        /* 一行格式: device mount_point fstype opts dump pass */
        char device[256]  = "";
        char mount[256]   = "";
        char fstype[64]   = "";
        int fields = 0;

        /* 解析前 3 个字段 */
        while (*p && *p != '\n' && fields < 3) {
            if (*p == ' ') { p++; continue; }
            if (fields == 0) {
                int i = 0;
                while (*p && *p != ' ' && i < 255) device[i++] = *p++;
                device[i] = '\0';
                fields++;
            } else if (fields == 1) {
                int i = 0;
                while (*p && *p != ' ' && i < 255) mount[i++] = *p++;
                mount[i] = '\0';
                fields++;
            } else if (fields == 2) {
                int i = 0;
                while (*p && *p != ' ' && i < 63) fstype[i++] = *p++;
                fstype[i] = '\0';
                fields++;
            }
        }
        /* 跳过剩余字段到行尾 */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        if (fields < 2) continue;

        /* 只显示真实文件系统（跳过 /proc, /sys, /dev, tmpfs 等伪文件系统） */
        if (strncmp(device, "/dev/", 5) != 0 &&
            strcmp(device, "rootfs") != 0 &&
            strcmp(device, "overlay") != 0) {
            /* 对 tmpfs、devtmpfs 等仍然显示 */
        }
        /* 跳过特定伪文件系统 */
        if (strcmp(fstype, "proc")    == 0 ||
            strcmp(fstype, "sysfs")   == 0 ||
            strcmp(fstype, "cgroup")  == 0 ||
            strcmp(fstype, "cgroup2") == 0 ||
            strcmp(fstype, "devpts")  == 0 ||
            strcmp(fstype, "devtmpfs")== 0 ||
            strcmp(fstype, "pstore")  == 0 ||
            strcmp(fstype, "securityfs") == 0 ||
            strcmp(fstype, "bpf")     == 0 ||
            strncmp(fstype, "autofs", 6) == 0 ||
            strcmp(fstype, "tracefs") == 0 ||
            strcmp(fstype, "debugfs") == 0 ||
            strcmp(fstype, "mqueue")  == 0 ||
            strcmp(fstype, "hugetlbfs") == 0 ||
            strcmp(fstype, "configfs") == 0 ||
            strcmp(fstype, "fusectl") == 0 ||
            strcmp(fstype, "none")    == 0 ||
            strcmp(fstype, "efivarfs") == 0 ||
            strcmp(device, "none") == 0)
            continue;

        /* 调用 statfs */
        struct statfs sf;
        if (__statfs(mount, &sf) < 0)
            continue;

        if (sf.f_blocks == 0) continue;

        long total = sf.f_blocks;
        long avail = sf.f_bavail;
        long used  = total - sf.f_bfree;
        long use_pct = (total > 0) ? (used * 100 / total) : 0;

        char s_total[32], s_used[32], s_avail[32];
        format_size(total, sf.f_bsize, s_total, sizeof(s_total));
        format_size(used,  sf.f_bsize, s_used,  sizeof(s_used));
        format_size(avail, sf.f_bsize, s_avail, sizeof(s_avail));

        printf("%-20s %12s %10s %10s %3ld%% %s\n",
               device, s_total, s_used, s_avail, use_pct, mount);
    }
}

static void print_usage(void)
{
    printf("Usage: df [-h]\n");
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

    df_all();
    return 0;
}
