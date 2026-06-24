#ifndef __SIG_NUM_H
#define __SIG_NUM_H

/* 标准信号编号 */
#define SIGHUP      1   /* 挂起 */
#define SIGINT      2   /* 中断 (Ctrl+C) */
#define SIGQUIT     3   /* 退出 */
#define SIGILL      4   /* 非法指令 */
#define SIGTRAP     5   /* 跟踪陷阱 */
#define SIGABRT     6   /* 异常终止 */
#define SIGBUS      7   /* 总线错误 */
#define SIGFPE      8   /* 浮点异常 */
#define SIGKILL     9   /* 杀死（不可屏蔽） */
#define SIGUSR1    10   /* 用户定义信号1 */
#define SIGSEGV    11   /* 段错误 */
#define SIGUSR2    12   /* 用户定义信号2 */
#define SIGPIPE    13   /* 管道破裂 */
#define SIGALRM    14   /* 闹钟信号 */
#define SIGTERM    15   /* 终止 */
#define SIGSTKFLT  16   /* 栈错误 */
#define SIGCHLD    17   /* 子进程状态改变 */
#define SIGCONT    18   /* 继续执行 */
#define SIGSTOP    19   /* 停止（不可屏蔽） */
#define SIGTSTP    20   /* 终端停止 (Ctrl+Z) */
#define SIGTTIN    21   /* 后台读终端 */
#define SIGTTOU    22   /* 后台写终端 */
#define SIGURG     23   /* 紧急数据 */
#define SIGXCPU    24   /* CPU时间超限 */
#define SIGXFSZ    25   /* 文件大小超限 */
#define SIGVTALRM  26   /* 虚拟闹钟 */
#define SIGPROF    27   /* 性能分析闹钟 */
#define SIGWINCH   28   /* 窗口大小改变 */
#define SIGIO      29   /* I/O可能 */
#define SIGPWR     30   /* 电源故障 */
#define SIGSYS     31   /* 系统调用错误 */

/* 用于屏蔽的信号 */
#define SIG_BLOCK   0   /* 阻塞信号 */
#define SIG_UNBLOCK 1   /* 解除阻塞 */
#define SIG_SETMASK 2   /* 设置信号掩码 */

#endif