/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * ps — 显示当前进程快照
 *
 * 机制：遍历 /proc (数字目录)，读取 status 和 stat，提取关键字段格式化输出。
 *       使用 tlibc_list_pids / tlibc_read_proc_status / tlibc_read_proc_stat。
 *
 * 系统调用：openat, read, close, getdents64
 *
 * 用法：
 *   ps          # 输出：UID, PID, PPID, STATE, RSS, TIME, COMMAND
 *
 * 索引：
 *   main            收集所有进程 → 排序 → 输出
 */

#include "tlibc_everything.h"
#include "procfs.h"

#define MAX_PROCS 2048

/* ── 进程信息缓存 ── */
struct proc_info {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    char  state;
    char  name[64];
    long  vm_rss_kb;
    char  time_str[16];   /* CPU 时间 MM:SS */
};

/* ── 按 PID 排序（插入排序，条目通常已排序） ── */
static void sort_procs(struct proc_info *info, int n)
{
    for (int i = 1; i < n; i++) {
        struct proc_info tmp = info[i];
        int j = i - 1;
        while (j >= 0 && info[j].pid > tmp.pid) {
            info[j + 1] = info[j];
            j--;
        }
        info[j + 1] = tmp;
    }
}

/* ── 输出 ── */
static void print_header(void)
{
    printf("%-8s %6s %6s %5s %8s %-8s %s\n",
           "UID", "PID", "PPID", "STATE", "RSS", "TIME", "COMMAND");
}

static void print_proc(struct proc_info *p)
{
    printf("%-8d %6d %6d %5c %8ld %-8s %s\n",
           p->uid, p->pid, p->ppid,
           p->state ? p->state : '?',
           p->vm_rss_kb >= 0 ? p->vm_rss_kb : 0,
           p->time_str, p->name);
}

static void print_usage(void)
{
    printf("Usage: ps\n");
}

int main(int argc, char *argv[])
{
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage();
        return 0;
    }

    /* 收集 PID 列表 */
    pid_t pids[MAX_PROCS];
    int n_pids = tlibc_list_pids(pids, MAX_PROCS);
    if (n_pids <= 0) {
        printf("ps: no processes found\n");
        return 1;
    }

    /* 收集进程信息 */
    struct proc_info info[MAX_PROCS];
    int n_info = 0;

    for (int i = 0; i < n_pids && n_info < MAX_PROCS; i++) {
        struct proc_status status;
        if (tlibc_read_proc_status(pids[i], &status) < 0)
            continue;

        info[n_info].pid       = status.pid;
        info[n_info].ppid      = status.ppid;
        info[n_info].uid       = status.uid;
        info[n_info].state     = status.state;
        info[n_info].vm_rss_kb = status.vm_rss_kb;

        strncpy(info[n_info].name, status.name, sizeof(info[n_info].name) - 1);
        info[n_info].name[sizeof(info[n_info].name) - 1] = '\0';

        /* CPU 时间 */
        struct proc_stat pstat;
        if (tlibc_read_proc_stat(pids[i], &pstat) == 0) {
            unsigned long total = pstat.utime + pstat.stime;
            tlibc_format_time(total, info[n_info].time_str,
                              sizeof(info[n_info].time_str));
        } else {
            snprintf(info[n_info].time_str, sizeof(info[n_info].time_str), "?:??");
        }

        n_info++;
    }

    sort_procs(info, n_info);
    print_header();
    for (int i = 0; i < n_info; i++)
        print_proc(&info[i]);

    return 0;
}
