/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * procfs.h - /proc filesystem parsing API
 *
 * Functions for reading process information from /proc.
 * Implemented in lib/procfs.c
 */

#ifndef __PROCFS_H
#define __PROCFS_H

#include "tlibc_types.h"

struct proc_status {
    char  name[64];        /* process name */
    pid_t pid;             /* process ID */
    pid_t ppid;            /* parent process ID */
    uid_t uid;             /* user ID */
    char  state;           /* state char: R, S, D, Z, T, X */
    long  vm_rss_kb;       /* VmRSS (kB) */
};

struct proc_stat {
    unsigned long utime;       /* user mode jiffies */
    unsigned long stime;       /* kernel mode jiffies */
    unsigned long starttime;   /* start jiffies (since boot) */
};

int  tlibc_list_pids(pid_t *pids, int max_pids);
int  tlibc_read_proc_status(pid_t pid, struct proc_status *out);
int  tlibc_read_proc_stat(pid_t pid, struct proc_stat *out);
unsigned long tlibc_jiffies_to_sec(unsigned long jiffies);
void tlibc_format_time(unsigned long jiffies, char *buf, int size);

#endif
