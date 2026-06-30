/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * grep — 在文件中搜索匹配的行
 *
 * 机制：逐行读取文件，用 strstr（或自定义大小写不敏感版本）匹配，
 *       输出匹配行。递归模式用 __getdents64 遍历目录。
 *
 * 系统调用：openat, read, close, getdents64
 *
 * 用法：
 *   grep <pattern> <file>         # 在文件中搜索
 *   grep <pattern> <file1> <file2> # 多文件搜索
 *   grep -i <pattern> <file>      # 忽略大小写
 *   grep -v <pattern> <file>      # 反向匹配（输出不匹配的行）
 *   grep -n <pattern> <file>      # 显示行号
 *   grep -r <pattern> <dir>       # 递归搜索目录
 *   grep -c <pattern> <file>      # 只输出匹配行数
 *
 * 索引：
 *   main               参数解析 → 对每个文件调用 grep_file
 *     grep_file        行 xx：打开 → 逐行匹配 → 输出
 *     match_line       行 xx：strstr 或 strcase_custom
 *     grep_recursive   行 xx：getdents64 递归遍历
 */

#include "tlibc_everything.h"
#include "ctype.h"

/* ── 全局选项 ── */
static int opt_ignore_case = 0;   /* -i */
static int opt_invert      = 0;   /* -v */
static int opt_line_number = 0;   /* -n */
static int opt_recursive   = 0;   /* -r */
static int opt_count       = 0;   /* -c */
static int opt_quiet       = 0;   /* -q (不输出，仅返回状态码) */

static const char *g_pattern = NULL;
static int g_pattern_len = 0;
static int g_exit_code = 1;  /* 1 = 无匹配 */

/* ── 临时缓冲区（BUF_SIZE，复用） ── */
#define GREP_BUF_SIZE 65536
static char g_buf[GREP_BUF_SIZE];

/* ── 大小写不敏感 strstr ── */
static char *stristr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n &&
               ((*h >= 'A' && *h <= 'Z') ? *h + 32 : *h) ==
               ((*n >= 'A' && *n <= 'Z') ? *n + 32 : *n)) {
            h++; n++;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

/* ── 匹配一行 ── */
static int match_line(const char *line)
{
    int matched;
    if (opt_ignore_case)
        matched = (stristr(line, g_pattern) != NULL);
    else
        matched = (strstr(line, g_pattern) != NULL);
    return opt_invert ? !matched : matched;
}

/* ── 输出一行（含可选文件名前缀和行号） ── */
static void print_line(const char *fname, int lineno, const char *line,
                       int show_fname)
{
    if (opt_quiet) return;
    if (show_fname)
        printf("%s:", fname);
    if (opt_line_number)
        printf("%d:", lineno);
    printf("%s", line);
    /* 如果行末没有换行，补一个 */
    int len = strlen(line);
    if (len == 0 || line[len - 1] != '\n')
        printf("\n");
}

/* ── 搜索单个文件 ── */
static int grep_file(const char *fname, int show_fname)
{
    int fd = openat(AT_FDCWD, fname, O_RDONLY, 0);
    if (fd < 0) {
        printf("grep: %s: No such file or directory\n", fname);
        return 0;
    }

    int count = 0;
    int lineno = 0;
    int pos = 0;
    int n;

    while ((n = read(fd, g_buf + pos, sizeof(g_buf) - pos - 1)) > 0) {
        int end = pos + n;
        g_buf[end] = '\0';

        int line_start = 0;
        for (int i = 0; i < end; i++) {
            if (g_buf[i] == '\n') {
                lineno++;
                g_buf[i] = '\0';
                if (match_line(g_buf + line_start)) {
                    count++;
                    g_exit_code = 0;
                    if (!opt_count)
                        print_line(fname, lineno, g_buf + line_start, show_fname);
                }
                line_start = i + 1;
            }
        }

        /* 未完成的行移到缓冲区开头 */
        int remaining = end - line_start;
        if (remaining > 0 && remaining < (int)sizeof(g_buf)) {
            __memmove(g_buf, g_buf + line_start, remaining);
            pos = remaining;
        } else {
            pos = 0;
        }
    }

    /* 处理最后一行（无换行结尾） */
    if (pos > 0) {
        g_buf[pos] = '\0';
        lineno++;
        if (match_line(g_buf)) {
            count++;
            g_exit_code = 0;
            if (!opt_count)
                print_line(fname, lineno, g_buf, show_fname);
        }
    }

    close(fd);

    if (opt_count && !opt_quiet) {
        if (show_fname)
            printf("%s:", fname);
        printf("%d\n", count);
    }

    return count;
}

/* ── 递归搜索目录 ── */
#define BUF_SZ 4096

static void grep_recursive(const char *dir_path)
{
    int fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (fd < 0) return;

    char dentry_buf[BUF_SZ];
    __memset(dentry_buf, 0, BUF_SZ);

    int n = getdents64(fd, (struct linux_dirent64 *)dentry_buf, BUF_SZ);
    close(fd);
    if (n < 0) return;

    struct linux_dirent64 *d = (struct linux_dirent64 *)dentry_buf;
    while (d->d_off != 0) {
        /* 跳过 . 和 .. */
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
            d = (struct linux_dirent64 *)((char *)d + d->d_reclen);
            continue;
        }

        /* 构造完整路径 */
        char full[1024];
        int dlen = strlen(dir_path);
        if (dlen > 0 && dir_path[dlen - 1] == '/')
            snprintf(full, sizeof(full), "%s%s", dir_path, d->d_name);
        else
            snprintf(full, sizeof(full), "%s/%s", dir_path, d->d_name);

        if (d->d_type == DT_DIR) {
            grep_recursive(full);
        } else if (d->d_type == DT_REG || d->d_type == DT_UNKNOWN) {
            grep_file(full, 1);
        }

        d = (struct linux_dirent64 *)((char *)d + d->d_reclen);
    }
}

/* ── 入口 ── */
static void print_usage(void)
{
    printf("Usage: grep [-i] [-v] [-n] [-r] [-c] <pattern> <file>...\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) { print_usage(); return 1; }

    /* 解析选项 */
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        char *p = argv[i] + 1;
        while (*p) {
            switch (*p) {
            case 'i': opt_ignore_case = 1; break;
            case 'v': opt_invert = 1; break;
            case 'n': opt_line_number = 1; break;
            case 'r': opt_recursive = 1; break;
            case 'c': opt_count = 1; break;
            case 'q': opt_quiet = 1; break;
            default:
                printf("grep: unknown option -%c\n", *p);
                return 1;
            }
            p++;
        }
        i++;
    }

    if (i >= argc) { print_usage(); return 1; }
    g_pattern = argv[i];
    g_pattern_len = strlen(g_pattern);
    i++;

    if (i >= argc) {
        /* 无文件参数，从 stdin 读取 */
        if (opt_recursive) { print_usage(); return 1; }
        int show_fname = 0;
        char *fname = "(standard input)";
        int count = 0;
        int lineno = 0;
        int pos = 0;
        int n;

        while ((n = read(STDIN, g_buf + pos, sizeof(g_buf) - pos - 1)) > 0) {
            int end = pos + n;
            g_buf[end] = '\0';
            int line_start = 0;
            for (int i = 0; i < end; i++) {
                if (g_buf[i] == '\n') {
                    lineno++;
                    g_buf[i] = '\0';
                    if (match_line(g_buf + line_start)) {
                        count++;
                        g_exit_code = 0;
                        if (!opt_count)
                            print_line(fname, lineno, g_buf + line_start, show_fname);
                    }
                    line_start = i + 1;
                }
            }
            int remaining = end - line_start;
            if (remaining > 0 && remaining < (int)sizeof(g_buf)) {
                __memmove(g_buf, g_buf + line_start, remaining);
                pos = remaining;
            } else {
                pos = 0;
            }
        }
        if (pos > 0) {
            g_buf[pos] = '\0';
            lineno++;
            if (match_line(g_buf)) {
                count++;
                g_exit_code = 0;
                if (!opt_count)
                    print_line(fname, lineno, g_buf, show_fname);
            }
        }
        if (opt_count && !opt_quiet) printf("%d\n", count);
        return g_exit_code;
    }

    int show_fname = (i < argc - 1) || opt_recursive;

    for (int j = i; j < argc; j++) {
        if (opt_recursive) {
            /* 检查 argv[j] 是文件还是目录 */
            struct stat st;
            if (stat(argv[j], &st) == 0 && S_ISDIR(st.st_mode))
                grep_recursive(argv[j]);
            else
                grep_file(argv[j], show_fname);
        } else {
            grep_file(argv[j], show_fname);
        }
    }

    return g_exit_code;
}
