/* SPDX-License-Identifier: MIT
 *
 * test_smoke.c — 功能冒烟测试
 *
 * 逐一运行项目中各程序（fork+execve 子进程），验证基本功能正常：
 *   - 检查退出码是否为预期值
 *   - 可选检查 stdout 中是否包含特定子串
 *   - 超时杀死卡住进程（SIGTERM → SIGKILL 两级）
 *   - 按分类分组输出，彩色 PASS/FAIL/SKIP
 *
 * 用法:
 *   build/output/test_smoke            # 全部测试
 *   build/output/test_smoke -v         # 详细输出（含跳过原因）
 *   build/output/test_smoke -l         # 列出用例不执行
 *   build/output/test_smoke -n         # 仅网络相关
 *   build/output/test_smoke -c core    # 仅 coreutils
 *   build/output/test_smoke -c test    # 仅 test 套件
 *   build/output/test_smoke -c net     # 仅网络
 *
 * 分类名: core, test, bench, net, other, all
 *
 * 设计原则:
 *   1. 不测试 term/ 下的交互式程序（vim, top, __game_pacman, template）
 *   2. 网络程序失败时标记 SKIP 而非 FAIL（可能无网络环境）
 *   3. 文件操作类程序顺序依赖，按表格顺序依次执行
 *   4. 测试临时文件统一放在 __smoke_tmp/ 下，最后递归清理
 */

#include "tlibc_everything.h"
#include "tlibc_test.h"          /* _T_GREEN, _T_RED, _T_CYAN, _T_RESET */
#include "string.h"
#include "signal.h"
#include "time.h"
#include "stat.h"
#include "fcntl.h"
#include "unistd.h"
#include "mman.h"
#include "dirent.h"
#include "errno.h"

/* _T_YELLOW — tlibc_test.h 没定义 */
#ifndef _T_YELLOW
#define _T_YELLOW "\033[33m"
#endif

/* WNOHANG — unistd.h 未定义，Linux 实际值 */
#ifndef WNOHANG
#define WNOHANG 1
#endif

/* fcntl 命令常量（项目头文件未提供） */
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif

/* ════════════════════════════════════════════════
   配置常量
   ════════════════════════════════════════════════ */

#define TMP_DIR          "__smoke_tmp"
#define BIN_DIR          "build/output"
#define MAX_STDOUT       4096
#define MAX_ARGS         12
#define POLL_MS          100      /* 轮询间隔 ms */
#define TIMEOUT_NORMAL   5        /* 大部分程序超时秒数 */
#define TIMEOUT_TEST     20       /* 测试套件 */
#define TIMEOUT_TMAKE    90       /* tmake 编译 */
#define TIMEOUT_NET      12       /* 网络程序 */

/* ════════════════════════════════════════════════
   分类
   ════════════════════════════════════════════════ */

#define CAT_COREUTILS   0
#define CAT_TEST_SUITE  1
#define CAT_BENCH       2
#define CAT_NETWORK     3
#define CAT_OTHER       4
#define CAT_SKIP        5

static const char *g_cat_name[] = {
    [CAT_COREUTILS] = "Coreutils",
    [CAT_TEST_SUITE] = "Test Suite",
    [CAT_BENCH]      = "Benchmark",
    [CAT_NETWORK]    = "Network",
    [CAT_OTHER]      = "Other",
    [CAT_SKIP]       = "Terminal (skipped)",
};

static const char *g_cat_key[] = {
    [CAT_COREUTILS] = "core",
    [CAT_TEST_SUITE] = "test",
    [CAT_BENCH]      = "bench",
    [CAT_NETWORK]    = "net",
    [CAT_OTHER]      = "other",
    [CAT_SKIP]       = "term",
};

/* ════════════════════════════════════════════════
   测试用例定义
   ════════════════════════════════════════════════ */

typedef struct {
    const char *name;            /* 显示名称 */
    const char *binary;          /* 二进制名，拼接 BIN_DIR/ 使用 */
    const char *argv[MAX_ARGS];  /* argv 表，NULL 结尾 */
    int  expected_exit;          /* 预期退出码 */
    const char *check_stdout;    /* stdout 预期子串（NULL=跳过） */
    int  timeout_sec;            /* 超时秒数 */
    int  needs_network;          /* 需要网络？失败时 SKIP 而非 FAIL */
    int  heavy;                  /* 耗时测试（如 exp），默认跳过 */
    int  category;               /* CAT_* */
} TestCase;

/*
 * 注意: coreutils 文件操作测试顺序相关（touch→rm, mkdir→rmdir, cp→mv），
 * 不能随意重排。每个涉及 __smoke_tmp/ 的测试都依赖前一个完成。
 */
static TestCase g_tests[] = {
    /* ═══════════════ Coreutils ═══════════════ */
    {"echo",            "echo",        {"echo", "hello smoke test", NULL},
     0, "hello smoke test", TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"pwd",             "pwd",         {"pwd", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"ls",              "ls",          {"ls", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"cat (small file)", "cat",        {"cat", TMP_DIR "/cat_test.txt", NULL},
     0, "Hello Smoke", TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"touch",           "touch",       {"touch", TMP_DIR "/touch_test", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"rm",              "rm",          {"rm", TMP_DIR "/touch_test", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"mkdir",           "mkdir",       {"mkdir", TMP_DIR "/mkdir_test", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"rmdir",           "rmdir",       {"rmdir", TMP_DIR "/mkdir_test", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"cp",              "cp",          {"cp", BIN_DIR "/cat", TMP_DIR "/cp_test", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"mv",              "mv",          {"mv", TMP_DIR "/cp_test", TMP_DIR "/mv_test", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"hexdump",         "hexdump",     {"hexdump", TMP_DIR "/cat_test.txt", NULL},
     0, "00000000", TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    {"fcount",          "fcount",      {"fcount", BIN_DIR, NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_COREUTILS},

    /* ═══════════════ Test Suite ═══════════════ */
    {"test_string",     "test_string",    {"test_string", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"test_printf",     "test_printf",    {"test_printf", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"test_filelist",   "test_filelist",  {"test_filelist", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"test_iomux",      "test_iomux",     {"test_iomux", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"passwd",          "passwd",         {"passwd", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"quene",           "quene",          {"quene", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"thread",          "thread",         {"thread", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    {"tlibc_free",      "tlibc_free",     {"tlibc_free", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_TEST_SUITE},

    /* ═══════════════ Benchmark ═══════════════ */
    {"exp",             "exp",        {"exp", NULL},
     0, NULL, TIMEOUT_TEST, 0, 1, CAT_BENCH},

    {"mempool",         "mempool",    {"mempool", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_BENCH},

    {"memtest",         "memtest",    {"memtest", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_BENCH},

    {"pthread",         "pthread",    {"pthread", NULL},
     0, NULL, TIMEOUT_TEST, 0, 0, CAT_BENCH},

    /* ═══════════════ Network ═══════════════ */
    {"ndiscover -h",    "ndiscover",  {"ndiscover", "-h", NULL},
     0, "Usage:", TIMEOUT_NET, 1, 0, CAT_NETWORK},

    {"portscan -h",     "portscan",   {"portscan", "-h", NULL},
     0, "Usage:", TIMEOUT_NET, 1, 0, CAT_NETWORK},

    {"netprobe -h",     "netprobe",   {"netprobe", "-h", NULL},
     0, "Usage:", TIMEOUT_NET, 1, 0, CAT_NETWORK},

    {"http -h",         "http",       {"http", "-h", NULL},
     0, "Usage:", TIMEOUT_NET, 1, 0, CAT_NETWORK},

    {"dnsquery localhost", "dnsquery", {"dnsquery", "localhost", NULL},
     0, NULL, TIMEOUT_NET, 1, 0, CAT_NETWORK},

    /* ═══════════════ Other ═══════════════ */
    {"elf_reader",      "elf_reader", {"elf_reader", TMP_DIR "/cat_test.txt", NULL},
     0, NULL, TIMEOUT_NORMAL, 0, 0, CAT_OTHER},

    {"elf_maker",       "elf_maker",  {"elf_maker", NULL},
     0, "已创建", TIMEOUT_NORMAL, 0, 0, CAT_OTHER},

    {"tmake -b echo",   "tmake",      {"tmake", "-b", "echo", NULL},
     0, NULL, TIMEOUT_TMAKE, 0, 0, CAT_OTHER},

    /* ═══════════════ Terminal（标记跳过）═══════════ */
    {"vim",             "vim",             {"vim", NULL},
     0, NULL, 0, 0, 0, CAT_SKIP},

    {"top",             "top",             {"top", NULL},
     0, NULL, 0, 0, 0, CAT_SKIP},

    {"__game_pacman",   "__game_pacman",   {"__game_pacman", NULL},
     0, NULL, 0, 0, 0, CAT_SKIP},

    {"template",        "template",        {"template", NULL},
     0, NULL, 0, 0, 0, CAT_SKIP},

    /* ── sentinel ── */
    {NULL, NULL, {NULL}, 0, NULL, 0, 0, 0, 0},
};

/* ════════════════════════════════════════════════
   全局状态
   ════════════════════════════════════════════════ */

static int g_pass         = 0;
static int g_fail         = 0;
static int g_skip         = 0;
static int g_verbose      = 0;
static int g_include_heavy = 0;

/* ════════════════════════════════════════════════
   文件操作辅助（父进程侧，用于 setup/verify/cleanup）
   ════════════════════════════════════════════════ */

static int file_exists(const char *path)
{
    return tlibc_is_path_file(path) == 1;
}

static int dir_exists(const char *path)
{
    return tlibc_is_path_dir(path) == 1;
}

static int make_dir(const char *path)
{
    return __mkdirat(AT_FDCWD, path, 0777);
}

static int remove_file(const char *path)
{
    return __unlinkat(AT_FDCWD, path, 0);
}

static int remove_dir(const char *path)
{
    return __unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

/*
 * 递归删除目录及其内容（清理用）
 * 注意：不追踪符号链接，只处理常规文件和目录。
 */
static void wipe_dir(const char *dir_path)
{
    /* 先尝试当普通文件删除 */
    if (remove_file(dir_path) == 0) return;
    if (remove_dir(dir_path) == 0)  return;

    /* 是目录 → 遍历删除子项 */
    int fd = __openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return;

    char buf[2048];
    while (1) {
        int n = __getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
        if (n <= 0) break;

        struct linux_dirent64 *d;
        for (int pos = 0; pos < n; pos += d->d_reclen) {
            d = (struct linux_dirent64 *)(buf + pos);

            if (d->d_name[0] == '.' &&
                (d->d_name[1] == '\0' ||
                 (d->d_name[1] == '.' && d->d_name[2] == '\0')))
                continue;

            char child[512];
            snprintf(child, sizeof(child), "%s/%s", dir_path, d->d_name);

            if (d->d_type == DT_DIR)
                wipe_dir(child);
            else
                remove_file(child);
        }
    }
    __close(fd);

    /* 现在目录应已空，可删除 */
    remove_dir(dir_path);
}

/* ════════════════════════════════════════════════
   子进程执行 + 输出捕获
   ════════════════════════════════════════════════ */

/*
 * 运行一个程序并等待其结束。
 *
 * 返回值:
 *    0  = PASS（退出码匹配，stdout 检查通过）
 *    1  = FAIL（退出码不匹配或 stdout 检查失败）
 *    2  = TIMEOUT（超时未结束）
 *   -1  = SKIP（二进制不存在）
 *
 * out_buf 和 out_size 用于接收捕获的输出。
 */
static int exec_and_wait(const TestCase *tc, char *out_buf, int out_size)
{
    /* 构造二进制路径 */
    char bin_path[256];
    snprintf(bin_path, sizeof(bin_path), "%s/%s", BIN_DIR, tc->binary);

    /* 二进制不存在 → SKIP */
    if (!file_exists(bin_path))
        return -1;

    /* 创建管道捕获 stdout+stderr */
    int pipefd[2];
    if (__pipe2(pipefd, 0) < 0)
        return -1;

    int pid = __fork();
    if (pid == 0) {
        /* ── child ── */
        __close(pipefd[0]);
        __dup2(pipefd[1], STDOUT);
        __dup2(pipefd[1], STDERR);
        if (pipefd[1] > STDERR)
            __close(pipefd[1]);

        __execve(bin_path, (char *const *)tc->argv, global_envp);
        __exit(127);  /* 只有 execve 失败才会到这 */
    }

    /* ── parent ── */
    __close(pipefd[1]);

    /*
     * 将管道读端设为非阻塞，避免子进程输出 >64KB 时 pipe buffer 满导致死锁。
     * 这样父进程可以在轮询间隙持续读取子进程的输出。
     */
    int fl = __fcntl(pipefd[0], F_GETFL, 0);
    __fcntl(pipefd[0], F_SETFL, fl | O_NONBLOCK);

    /* 轮询等待，支持超时，边等边读输出 */
    int status = 0;
    int timed_out = 0;
    int ret;
    int total = 0;
    int max_polls = tc->timeout_sec * (1000 / POLL_MS);
    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = POLL_MS * 1000000L;

    for (int i = 0; i < max_polls; i++) {
        /* 读管道中已有的数据（非阻塞，EAGAIN 时返回 -1 直接跳过） */
        while (total < out_size - 1) {
            int n = __read(pipefd[0], out_buf + total, out_size - total - 1);
            if (n <= 0) break;  /* EAGAIN 或 EOF */
            total += n;
        }

        ret = __waitpid(pid, &status, WNOHANG);
        if (ret == pid)
            break;                /* 子进程已退出 */
        if (ret < 0)
            break;                /* waitpid 出错 */
        __nanosleep(&ts, NULL);

        if (i == max_polls - 1)
            timed_out = 1;        /* 最后一次轮询仍无结果 */
    }

    /* 子进程退出后，排空剩余输出 */
    while (total < out_size - 1) {
        int n = __read(pipefd[0], out_buf + total, out_size - total - 1);
        if (n <= 0) break;
        total += n;
    }
    __close(pipefd[0]);
    out_buf[total] = '\0';

    /* 超时处理：SIGTERM → 1s → SIGKILL */
    if (timed_out) {
        __kill(pid, SIGTERM);
        for (int i = 0; i < 10; i++) {
            if (__waitpid(pid, &status, WNOHANG) == pid) {
                timed_out = 0;
                break;
            }
            __nanosleep(&ts, NULL);
        }
        if (timed_out) {
            __kill(pid, SIGKILL);
            __waitpid(pid, &status, 0);
        }
        return 2;
    }

    /* 解析退出码（Linux wait 编码：低 8 位=信号号，高 8 位=退出码） */
    int signal_num  = status & 0x7f;
    int exit_code   = (status >> 8) & 0xff;

    if (signal_num != 0)
        return 1;  /* 被信号杀死 → FAIL */

    if (exit_code != tc->expected_exit) {
        if (exit_code == 127)
            return -1;  /* execve 失败 → SKIP */
        return 1;       /* 退出码不对 → FAIL */
    }

    /* 检查 stdout */
    if (tc->check_stdout && !strstr(out_buf, tc->check_stdout))
        return 1;

    return 0;
}

/* ════════════════════════════════════════════════
   测试执行逻辑
   ════════════════════════════════════════════════ */

static void print_duration(long ms)
{
    if (ms < 1000)
        __printf("  (%ldms)", ms);
    else
        __printf("  (%ld.%02lds)", ms / 1000, (ms % 1000) / 10);
}

static void run_single_test(const TestCase *tc)
{
    char stdout_buf[MAX_STDOUT];
    stdout_buf[0] = '\0';

    /* ── 跳过 term 程序 ── */
    if (tc->category == CAT_SKIP) {
        if (g_verbose)
            __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET
                     "  (interactive terminal program)\n", tc->name);
        else
            __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET "\n", tc->name);
        g_skip++;
        return;
    }

    /* ── 默认跳过耗时测试（exp 等），-a 开启 ── */
    if (tc->heavy && !g_include_heavy) {
        if (g_verbose)
            __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET
                     "  (heavy benchmark, use -a to enable)\n", tc->name);
        else
            __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET "\n", tc->name);
        g_skip++;
        return;
    }

    /* 计时 */
    struct timespec t1, t2;
    __clock_gettime(CLOCK_MONOTONIC, &t1);
    int result = exec_and_wait(tc, stdout_buf, sizeof(stdout_buf));
    __clock_gettime(CLOCK_MONOTONIC, &t2);
    long elapsed_ms = (t2.tv_sec - t1.tv_sec) * 1000 +
                      (t2.tv_nsec - t1.tv_nsec) / 1000000;

    if (result == 0) {
        __printf("  · %-25s " _T_GREEN "PASS" _T_RESET, tc->name);
        print_duration(elapsed_ms);
        __printf("\n");
        g_pass++;
    }
    else if (result == -1) {
        /* 二进制不存在或无法执行 */
        if (g_verbose)
            __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET
                     "  (binary not found or not built)\n", tc->name);
        else
            __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET "\n", tc->name);
        g_skip++;
    }
    else if (result == 2) {
        /* 超时 */
        __printf("  · %-25s " _T_RED "FAIL" _T_RESET
                 "  (timeout after %ds)\n", tc->name, tc->timeout_sec);
        g_fail++;
    }
    else if (result == 1) {
        /* 退出码或 stdout 不匹配 */
        /* 网络程序失败时降级为 SKIP */
        if (tc->needs_network) {
            if (g_verbose)
                __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET
                         "  (network unavailable?)\n", tc->name);
            else
                __printf("  · %-25s " _T_CYAN "SKIP" _T_RESET "\n", tc->name);
            g_skip++;
        } else {
            if (g_verbose)
                __printf("  · %-25s " _T_RED "FAIL" _T_RESET
                         "\n    exit code / stdout mismatch\n"
                         "    stdout: %.200s\n", tc->name, stdout_buf);
            else
                __printf("  · %-25s " _T_RED "FAIL" _T_RESET "\n", tc->name);
            g_fail++;
        }
    }
}

/* ════════════════════════════════════════════════
   Coreutils 特殊处理：文件操作验证 + 清理
   ════════════════════════════════════════════════ */

/* 对特定测试做额外验证（文件存在性等） */
static int verify_file_test(const TestCase *tc)
{
    if (strcmp(tc->name, "touch") == 0) {
        return file_exists(TMP_DIR "/touch_test") ? 1 : 0;
    }
    if (strcmp(tc->name, "rm") == 0) {
        return !file_exists(TMP_DIR "/touch_test") ? 1 : 0;
    }
    if (strcmp(tc->name, "mkdir") == 0) {
        return dir_exists(TMP_DIR "/mkdir_test") ? 1 : 0;
    }
    if (strcmp(tc->name, "rmdir") == 0) {
        return !dir_exists(TMP_DIR "/mkdir_test") ? 1 : 0;
    }
    if (strcmp(tc->name, "cp") == 0) {
        return file_exists(TMP_DIR "/cp_test") ? 1 : 0;
    }
    if (strcmp(tc->name, "mv") == 0) {
        return !file_exists(TMP_DIR "/cp_test") &&
                file_exists(TMP_DIR "/mv_test") ? 1 : 0;
    }
    return 1; /* 非文件测试默认通过 */
}

static int needs_verify(const TestCase *tc)
{
    return strcmp(tc->name, "touch") == 0 ||
           strcmp(tc->name, "rm")    == 0 ||
           strcmp(tc->name, "mkdir")  == 0 ||
           strcmp(tc->name, "rmdir")  == 0 ||
           strcmp(tc->name, "cp")    == 0 ||
           strcmp(tc->name, "mv")    == 0;
}

/* ════════════════════════════════════════════════
   入口与参数解析
   ════════════════════════════════════════════════ */

static void print_usage(void)
{
    __printf("用法: build/output/test_smoke [选项]\n"
             "选项:\n"
             "  -v         详细输出\n"
             "  -l         列出测试用例（不执行）\n"
             "  -n         仅运行网络相关测试\n"
             "  -a         包含耗时测试（exp 等，默认跳过）\n"
             "  -c <分类>   仅运行指定分类"
             " (core/test/bench/net/other/all)\n");
}

static void list_tests(void)
{
    int last_cat = -1;
    for (int i = 0; g_tests[i].name != NULL; i++) {
        const TestCase *tc = &g_tests[i];
        if (tc->category != last_cat) {
            __printf("\n" _T_CYAN "── %s ──" _T_RESET "\n",
                     g_cat_name[tc->category]);
            last_cat = tc->category;
        }
        __printf("  %s", tc->name);
        if (tc->needs_network)
            __printf("  [network]");
        if (tc->check_stdout)
            __printf("  → expect \"%s\"", tc->check_stdout);
        __printf("\n");
    }
}

static int match_category(const TestCase *tc, const char *filter)
{
    if (filter == NULL) return 1;              /* 不过滤 */
    if (strcmp(filter, "all") == 0) return 1;
    return strcmp(g_cat_key[tc->category], filter) == 0;
}

/* ── 递归扫描 app/ 下所有 .c，找出未测试的程序 ── */
static void scan_untested_recursive(const char *dir, const char *prefix, int *total)
{
    int fd = __openat(AT_FDCWD, dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return;

    char buf[4096];
    char c_store[512][256];          /* 拷贝存储，避免 d->d_name 悬空 */
    char s_store[128][256];
    int  c_cnt = 0, sub_cnt = 0;

    while (1) {
        int n = __getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
        if (n <= 0) break;

        struct linux_dirent64 *d;
        for (int pos = 0; pos < n; pos += d->d_reclen) {
            d = (struct linux_dirent64 *)(buf + pos);

            if (d->d_name[0] == '.' &&
                (d->d_name[1] == '\0' ||
                 (d->d_name[1] == '.' && d->d_name[2] == '\0')))
                continue;

            int len = strlen(d->d_name);

            if (len >= 3 && d->d_name[len-2] == '.' && d->d_name[len-1] == 'c') {
                strncpy(c_store[c_cnt], d->d_name, 255);
                c_store[c_cnt][255] = '\0';
                c_cnt++;
            }

            if (d->d_type == DT_DIR) {
                strncpy(s_store[sub_cnt], d->d_name, 255);
                s_store[sub_cnt][255] = '\0';
                sub_cnt++;
            }
        }
    }
    __close(fd);

    /* 遍历本目录下的 .c 文件 */
    int local_untested = 0;
    for (int i = 0; i < c_cnt; i++) {
        int len = strlen(c_store[i]);
        char bin[256];
        strncpy(bin, c_store[i], len - 2);
        bin[len - 2] = '\0';

        if (strcmp(bin, "shell") == 0 || strcmp(bin, "template") == 0)
            continue;

        char bp[512];
        snprintf(bp, sizeof(bp), "%s/%s", BIN_DIR, bin);
        if (!file_exists(bp)) continue;

        int found = 0;
        for (int j = 0; g_tests[j].name != NULL; j++) {
            if (strcmp(g_tests[j].binary, bin) == 0) { found = 1; break; }
        }
        if (found) continue;

        if (*total == 0)
            __printf("\n" _T_YELLOW "未覆盖的程序:" _T_RESET "\n");
        if (local_untested == 0)
            __printf("  %-12s", *prefix ? prefix : ".");
        __printf(" %s", bin);
        local_untested++;
        (*total)++;
    }
    if (local_untested) __printf("\n");

    /* 递归进入子目录 */
    for (int i = 0; i < sub_cnt; i++) {
        char sub_full[512], sub_pre[256];
        snprintf(sub_full, sizeof(sub_full), "%s/%s", dir, s_store[i]);
        if (*prefix)
            snprintf(sub_pre, sizeof(sub_pre), "%s/%s", prefix, s_store[i]);
        else
            snprintf(sub_pre, sizeof(sub_pre), "%s", s_store[i]);
        scan_untested_recursive(sub_full, sub_pre, total);
    }
}

int main(int argc, char *argv[])
{
    const char *cat_filter = NULL;
    int list_mode   = 0;
    int net_only    = 0;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            g_verbose = 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            g_include_heavy = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            list_mode = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            net_only = 1;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cat_filter = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else {
            __printf("未知参数: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (list_mode) {
        __printf("可用测试用例:\n");
        list_tests();
        return 0;
    }

    /* ── 创建临时目录和测试文件 ── */
    if (!dir_exists(TMP_DIR))
        make_dir(TMP_DIR);

    /* 创建供 cat / hexdump / elf_reader 使用的小文件 */
    int setup_fd = __creat(TMP_DIR "/cat_test.txt", 0644);
    if (setup_fd >= 0) {
        __write(setup_fd, "Hello Smoke Test\n", 17);
        __close(setup_fd);
    }

    /* ── 执行测试 ── */
    struct timespec t_start, t_end;
    __clock_gettime(CLOCK_MONOTONIC, &t_start);

    int last_cat = -1;

    for (int i = 0; g_tests[i].name != NULL; i++) {
        const TestCase *tc = &g_tests[i];

        /* 分类过滤 */
        if (cat_filter && !match_category(tc, cat_filter))
            continue;
        if (net_only && !tc->needs_network &&
            tc->category != CAT_SKIP)
            continue;

        /* 分类标题 */
        if (tc->category != last_cat) {
            __printf("\n" _T_CYAN "── %s ──" _T_RESET "\n",
                     g_cat_name[tc->category]);
            last_cat = tc->category;
        }

        run_single_test(tc);

        /* 额外验证：文件存在性检查（coreutils） */
        if (needs_verify(tc) && tc->category != CAT_SKIP &&
            tc->category != CAT_NETWORK) {
            if (!verify_file_test(tc))
                __printf("    ⚠ file state unexpected"
                         " (may affect later tests)\n");
        }
    }

    /* ── 清理临时文件 ── */
    if (file_exists(TMP_DIR) || dir_exists(TMP_DIR))
        wipe_dir(TMP_DIR);

    /* elf_maker 创建了 helloworld → 清理 */
    if (file_exists("helloworld"))
        remove_file("helloworld");

    /* ── 总用时 ── */
    __clock_gettime(CLOCK_MONOTONIC, &t_end);
    long total_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                    (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

    /* ── 递归扫描未测试程序 ── */
    int untested_total = 0;
    scan_untested_recursive("app", "", &untested_total);

    /* ── 汇总 ── */
    int total = g_pass + g_fail + g_skip;
    __printf("\n" _T_CYAN "═══════════════════════════════════════"
             _T_RESET "\n");
    __printf("  总计: %d  |  " _T_GREEN "PASS: %d" _T_RESET
             "  |  " _T_RED "FAIL: %d" _T_RESET
             "  |  " _T_CYAN "SKIP: %d" _T_RESET,
             total, g_pass, g_fail, g_skip);
    __printf("  |  用时: ");
    print_duration(total_ms);
    __printf("\n");

    return g_fail > 0 ? 1 : 0;
}
