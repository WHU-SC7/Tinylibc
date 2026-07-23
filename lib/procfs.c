/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * procfs.c - /proc filesystem parsing library
 *
 * Reads /proc/[pid]/status and /proc/[pid]/stat text content,
 * extracts fields via string parsing. Enumerates PIDs by scanning
 * /proc numerical directory entries.
 *
 * Index:
 *   tlibc_list_pids            scan /proc for PIDs
 *   tlibc_read_proc_status     parse /proc/[pid]/status: Name, Pid, Ppid, Uid, State, VmRSS
 *   tlibc_read_proc_stat       parse /proc/[pid]/stat: utime, stime, starttime
 */

#include "tlibc_everything.h"

/* ── 进程状态（解析结果） ── */
struct proc_status {
    char  name[64];        /* 进程名 */
    pid_t pid;             /* 进程 ID */
    pid_t ppid;            /* 父进程 ID */
    uid_t uid;             /* 用户 ID */
    char  state;           /* 状态字符: R, S, D, Z, T, X */
    long  vm_rss_kb;       /* VmRSS (kB) */
};

struct proc_stat {
    unsigned long utime;       /* 用户态 jiffies */
    unsigned long stime;       /* 内核态 jiffies */
    unsigned long starttime;   /* 启动 jiffies（相对于系统启动） */
};

/* 内核时钟频率（用于 jiffies → 秒转换） */
#define CLK_TCK 100

/* ── 按 PID 构建 /proc 路径 ── */
static void make_proc_path(pid_t pid, const char *suffix, char *buf, int size)
{
    snprintf(buf, size, "/proc/%d%s", pid, suffix);
}

/* ── 读取文件全部内容到缓冲区 ── */
static int read_file_all(const char *path, char *buf, int size)
{
    int fd = openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    int total = 0;
    int n;
    while (total < size - 1 && (n = read(fd, buf + total, size - total - 1)) > 0)
        total += n;
    close(fd);
    if (total < 0) return -1;
    buf[total] = '\0';
    return total;
}

/* ── 在 key: value 格式的文本中查找指定 key ── */
static const char *find_status_field(const char *buf, const char *key)
{
    int klen = strlen(key);
    const char *p = buf;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == ':') {
            p += klen + 1;
            while (*p == ' ' || *p == '\t') p++;
            return p;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return NULL;
}

/* ── 从 "key:\tvalue\n" 中提取数值 ── */
static long extract_num_field(const char *buf, const char *key)
{
    const char *val = find_status_field(buf, key);
    if (!val) return -1;
    long n = 0;
    while (*val >= '0' && *val <= '9') {
        n = n * 10 + (*val - '0');
        val++;
    }
    return n;
}

/* ── 列出所有进程 PID ── */
int tlibc_list_pids(pid_t *pids, int max_pids)
{
    int fd = openat(AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (fd < 0) return -1;

    char buf[4096];
    __memset(buf, 0, sizeof(buf));
    int n = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
    close(fd);
    if (n < 0) return -1;

    int count = 0;
    char *end = buf + n;
    struct linux_dirent64 *d = (struct linux_dirent64 *)buf;
    while ((char *)d < end && count < max_pids) {
        if (d->d_type == DT_DIR) {
            /* 检查是否为纯数字 */
            int is_num = 1;
            for (char *p = d->d_name; *p; p++) {
                if (*p < '0' || *p > '9') { is_num = 0; break; }
            }
            if (is_num) {
                pids[count++] = (pid_t)atol(d->d_name);
            }
        }
        d = (struct linux_dirent64 *)((char *)d + d->d_reclen);
    }
    return count;
}

/* ── 读取进程状态（/proc/[pid]/status） ── */
int tlibc_read_proc_status(pid_t pid, struct proc_status *out)
{
    char path[64];
    char buf[4096];

    make_proc_path(pid, "/status", path, sizeof(path));
    if (read_file_all(path, buf, sizeof(buf)) < 0)
        return -1;

    __memset(out, 0, sizeof(*out));

    /* Name */
    const char *v = find_status_field(buf, "Name");
    if (v) {
        int i = 0;
        while (*v && *v != '\n' && i < (int)sizeof(out->name) - 1)
            out->name[i++] = *v++;
        out->name[i] = '\0';
    }

    /* Pid, PPid */
    out->pid  = (pid_t)extract_num_field(buf, "Pid");
    out->ppid = (pid_t)extract_num_field(buf, "PPid");

    /* Uid (real uid, 第一个数字) */
    v = find_status_field(buf, "Uid");
    if (v) {
        out->uid = 0;
        while (*v >= '0' && *v <= '9') {
            out->uid = out->uid * 10 + (*v - '0');
            v++;
        }
    }

    /* State (第一个字符) */
    v = find_status_field(buf, "State");
    if (v) out->state = *v;

    /* VmRSS (kB) */
    out->vm_rss_kb = extract_num_field(buf, "VmRSS");

    return 0;
}

/* ── 读取进程统计（/proc/[pid]/stat） ── */
int tlibc_read_proc_stat(pid_t pid, struct proc_stat *out)
{
    char path[64];
    char buf[2048];

    make_proc_path(pid, "/stat", path, sizeof(path));
    if (read_file_all(path, buf, sizeof(buf)) < 0)
        return -1;

    __memset(out, 0, sizeof(*out));

    /* 格式: pid (comm) state ppid ... */
    /* 跳过 pid */
    char *p = buf;
    while (*p && *p != ' ') p++;
    if (*p) p++;
    /* 跳过 (comm) */
    if (*p == '(') {
        while (*p && *p != ')') p++;
        if (*p) p += 2;  /* 跳过 ") " */
    }
    /* 跳过 state */
    while (*p && *p != ' ') p++;
    if (*p) p++;

    /* ppid */
    /* 已跳过 4 个字段: pid, comm, state, ppid */
    /* 再跳过 4 个字段到达 utime */
    for (int i = 0; i < 4; i++) {
        while (*p && *p != ' ') p++;
        if (*p) p++;
    }

    /* 现在 p 指向 utime */
    out->utime = 0;
    while (*p && *p >= '0' && *p <= '9') {
        out->utime = out->utime * 10 + (*p - '0');
        p++;
    }
    if (*p) p++;

    /* stime */
    out->stime = 0;
    while (*p && *p >= '0' && *p <= '9') {
        out->stime = out->stime * 10 + (*p - '0');
        p++;
    }
    if (*p) p++;

    /* 跳到 starttime: 跳过 cutime(14), cstime(15), priority(16), nice(17), numthreads(18), itrealvalue(19) */
    for (int i = 0; i < 6; i++) {
        while (*p && *p != ' ') p++;
        if (*p) p++;
    }

    /* starttime (第21个字段, 0-based 20) */
    out->starttime = 0;
    while (*p && *p >= '0' && *p <= '9') {
        out->starttime = out->starttime * 10 + (*p - '0');
        p++;
    }

    return 0;
}

/* ── 将 jiffies 转换为秒 ── */
unsigned long tlibc_jiffies_to_sec(unsigned long jiffies)
{
    return jiffies / CLK_TCK;
}

/* ── 将 jiffies 转换为 "MM:SS" 格式 ── */
void tlibc_format_time(unsigned long jiffies, char *buf, int size)
{
    unsigned long total_sec = tlibc_jiffies_to_sec(jiffies);
    unsigned long min = total_sec / 60;
    unsigned long sec = total_sec % 60;
    snprintf(buf, size, "%ld:%02ld", (long)min, (long)sec);
}
