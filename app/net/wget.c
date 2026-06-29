/*
 * wget — HTTP 文件下载工具
 *
 * 机制：解析 HTTP URL → DNS 解析 → TCP 连接 →
 *       GET/HEAD 请求 → 接收响应头 → 读取 body 写入文件。
 *       支持断点续传（Range）、多线程分块下载（Range 分片）、
 *       HTTP 代理、chunked transfer、递进式进度条。
 *
 * 系统调用：socket, connect, read, write, close, openat, lseek, ftruncate
 * 线程 API：pthread_create, pthread_join（并行分块下载）
 *
 * 用法：
 *   wget http://example.com/file.zip                 # 下载到当前目录
 *   wget -O output.zip http://example.com/file.zip    # 指定输出文件名
 *   wget -c -O output.zip http://example.com/file.zip # 断点续传
 *   wget --parallel 4 -O file http://example.com/big  # 4 线程分块下载
 *   wget -x http://proxy:8080 http://example.com/file # HTTP 代理
 *
 * 注意：不支持 HTTPS，仅 HTTP。需要网络连接。
 *       多线程下载要求服务器支持 Range 头。
 *
 * 索引：
 *   main                入口：参数解析 → URL 解析 → HEAD 获取信息
 *                        → 单线程/多线程下载
 *     parse_url         行 102  URL 分解为 host:port/path
 *     tcp_connect       行 184  DNS 解析 + socket + connect
 *     http_request      行 376  发送 GET/HEAD + 接收响应头
 *     recv_headers      行 306  解析 HTTP 响应头（Content-Length,
 *                               Transfer-Encoding, Location 等）
 *     read_fixed        行 438  按指定长度读取 body
 *     read_chunked      行 392  读取 chunked 编码数据
 *     download_thread   行 563  并行分块下载线程入口
 *     show_progress_line 行 480  终端进度条绘制
 */

#include "tlibc_everything.h"
#include "atomic.h"
#include "tty.h"

/* ── 常量 ── */
#define WGET_BUF_SIZE      (64 * 1024)   /* 读缓冲区 64KB */
#define WGET_HDR_BUF       8192           /* 响应头缓冲区 */

/* ── 全局状态（静默模式/进度条） ── */
static int g_quiet = 0;

/* ================================================================
 *  工具函数
 * ================================================================ */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] <url>\n", prog);
    printf("Options:\n");
    printf("  -O <file>        输出到指定文件\n");
    printf("  -c               断点续传\n");
    printf("  -q               静默模式\n");
    printf("  --parallel <N>   并发下载线程数\n");
    printf("  -x <proxy>       HTTP 代理 (http://host:port)\n");
    printf("  -h, --help       显示此帮助\n");
    printf("\nExample:\n");
    printf("  %s http://example.com/file.zip\n", prog);
    printf("  %s -O output.zip http://example.com/file.zip\n", prog);
    printf("  %s -c -O output.zip http://example.com/file.zip\n", prog);
    printf("  %s --parallel 4 -O output.zip http://example.com/file.zip\n", prog);
    printf("  %s -x http://proxy:8080 http://example.com/file.zip\n", prog);
}

/* 获取终端宽度（列数），失败返回 80 */
static int get_term_width(void)
{
    struct winsize ws;
    if (tlibc_get_term_size(1, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/* 格式化字节大小（如 "559 B", "1.2 K", "14.8 M", "1.0 G"）
 * 避免使用 snprintf %f（精度控制有限），纯整数算术 */
static void format_size(char *buf, int sz, long long bytes)
{
    const char *units[] = {"B", "K", "M", "G", "T"};
    int ui = 0;
    long long divisor = 1;

    while (bytes >= divisor * 1024 && ui < 4) {
        divisor *= 1024;
        ui++;
    }

    if (ui == 0) {
        snprintf(buf, sz, "%ld B", (long)bytes);
        return;
    }

    /* 定点数：一位小数，四舍五入 */
    long long int_part = bytes / divisor;
    long long frac_part = ((bytes % divisor) * 10 + divisor / 2) / divisor;

    if (frac_part >= 10) {
        int_part++;
        frac_part = 0;
    }

    if (int_part >= 100 || frac_part == 0)
        snprintf(buf, sz, "%ld %s", (long)int_part, units[ui]);
    else
        snprintf(buf, sz, "%ld.%ld %s", (long)int_part, (long)frac_part, units[ui]);
}

/* 格式化时长（秒 → HH:MM:SS 或 MM:SS） */
static void format_time(char *buf, int sz, int secs)
{
    if (secs >= 3600)
        snprintf(buf, sz, "%02d:%02d:%02d", secs / 3600, (secs % 3600) / 60, secs % 60);
    else
        snprintf(buf, sz, "%02d:%02d", secs / 60, secs % 60);
}

/* ================================================================
 *  URL 解析
 * ================================================================
 * 解析 http://host[:port]/path 格式，分解出各组成部分 */
static int parse_url(const char *url, char *host, int host_sz,
                     int *port, char *path, int path_sz)
{
    const char *p = url;

    /* 跳过协议前缀 */
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        printf("Error: HTTPS not supported (use HTTP)\n");
        return -1;
    }

    /* 提取 host */
    const char *host_start = p;
    const char *host_end;
    int path_has_slash = 0;

    /* 查找 host 结束位置（: 或 / 或 结尾） */
    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');

    if (colon && (!slash || colon < slash)) {
        /* host:port/path 形式 */
        host_end = colon;
        p = colon + 1;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
        if (*port == 0) *port = 80;
        if (*p == '/') { path_has_slash = 1; p++; }
    } else if (slash) {
        host_end = slash;
        path_has_slash = 1;
        p = slash + 1;
    } else {
        host_end = p + strlen(p);
        p = host_end;
    }

    int hlen = host_end - host_start;
    if (hlen <= 0 || hlen >= host_sz) return -1;
    strncpy(host, host_start, hlen);
    host[hlen] = '\0';

    if (*port == 0) *port = 80;

    /* path */
    if (path_has_slash) {
        /* 剩余部分是 path（不含前导的 / 已在上面跳过） */
        int plen = (int)strlen(p);
        if (plen + 2 > path_sz) return -1;
        path[0] = '/';
        strncpy(path + 1, p, plen);
        path[plen + 1] = '\0';
    } else {
        strncpy(path, "/", path_sz - 1);
        path[path_sz - 1] = '\0';
    }

    return 0;
}

/* ================================================================
 *  网络连接 — DNS 解析 → socket → connect
 * ================================================================ */

/* 解析主机名（域名或点分十进制）→ IP 地址 */
static int resolve_host(const char *host, uint32_t *ip_out)
{
    uint32_t ip = tlibc_inet_addr(host);
    if (ip != 0xFFFFFFFF) {
        *ip_out = ip;
        return 0;
    }
    /* DNS 解析 */
    return tlibc_dns_resolve(host, ip_out);
}

/* 创建 TCP 连接到 host:port，返回 fd（<0 表示失败） */
static int tcp_connect(const char *host, int port)
{
    uint32_t ip;
    if (resolve_host(host, &ip) != 0) {
        if (!g_quiet)
            printf("wget: failed to resolve '%s'\n", host);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = tlibc_htons(port);
    addr.sin_addr.s_addr = ip;

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        __close(sock);
        return -1;
    }
    return sock;
}

/* 连接目标（直接或通过代理） */
static int connect_target(const char *host, int port,
                          const char *proxy_host, int proxy_port)
{
    if (proxy_host && proxy_host[0])
        return tcp_connect(proxy_host, proxy_port);
    else
        return tcp_connect(host, port);
}

/* ================================================================
 *  HTTP 请求/响应 — 发送请求行 + 解析响应头
 * ================================================================ */

/* 发送 HTTP 请求，返回写入的字节数（<0 表示错误） */
static int send_http_request(int sock, const char *method,
                             const char *req_path, const char *host_header,
                             const char *extra_headers)
{
    char buf[4096];
    int n;

    if (extra_headers && extra_headers[0])
        n = snprintf(buf, sizeof(buf),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "%s"
            "Connection: keep-alive\r\n"
            "\r\n", method, req_path, host_header, extra_headers);
    else
        n = snprintf(buf, sizeof(buf),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: keep-alive\r\n"
            "\r\n", method, req_path, host_header);

    return (int)__write(sock, buf, n);
}

/* 读取一行（以 \r\n 或 \n 结尾），返回行长度（不含 \r\n），
 * 或 <0 表示错误/EOF。buf 始终以 NUL 结尾 */
static int read_line(int sock, char *buf, int buf_sz)
{
    int i = 0;
    char c;
    while (i < buf_sz - 1) {
        int n = (int)__read(sock, &c, 1);
        if (n <= 0) break;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

/* 解析 HTTP 状态行，返回状态码（如 200），<0 表示解析失败 */
static int parse_status_line(const char *line)
{
    if (strncmp(line, "HTTP/", 5) != 0) return -1;
    const char *p = line + 5;
    /* 跳过版本号 */
    while (*p && *p != ' ') p++;
    if (!*p) return -1;
    p++; /* 跳过空格 */
    int code = 0;
    while (*p >= '0' && *p <= '9') {
        code = code * 10 + (*p - '0');
        p++;
    }
    return code;
}

/* strncasecmp — 不区分大小写的有限比较 */
static int strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = (unsigned char)a[i];
        int cb = (unsigned char)b[i];
        if (ca == 0 && cb == 0) return 0;
        if (ca >= 'A' && ca <= 'Z') ca += 0x20;
        if (cb >= 'A' && cb <= 'Z') cb += 0x20;
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

/* 解析响应头，填充 headers 缓冲区（连续存放，每行 \0 结尾，最后空行标记）
 * 返回 body 开始位置在 buf 中的偏移，<0 表示错误 */
struct http_response {
    int  status_code;
    long long content_length;   /* -1 表示未知 */
    int  chunked;               /* Transfer-Encoding: chunked */
    int  accept_ranges;         /* Accept-Ranges: bytes */
    int  location_offset;       /* 重定向 URL 在 headers 缓冲区中的偏移，0 表示无 */
    int  headers_len;           /* 头部总长度（含空行） */
};

static int recv_headers(int sock, char *buf, int buf_sz,
                        struct http_response *resp)
{
    int pos = 0;
    int first_line = 1;
    int status = 0;

    resp->status_code = 0;
    resp->content_length = -1;
    resp->chunked = 0;
    resp->accept_ranges = 0;
    resp->location_offset = 0;
    resp->headers_len = 0;

    while (pos < buf_sz - 4) {
        /* 读取一行 */
        int line_start = pos;
        int n = read_line(sock, buf + pos, buf_sz - pos);
        if (n < 0) return -1;
        pos += n;
        if (pos >= buf_sz) return -1;
        buf[pos++] = '\0'; /* 行终止符 */

        /* 空行 → 头部结束 */
        if (n == 0) break;

        char *line = buf + line_start;

        if (first_line) {
            status = parse_status_line(line);
            if (status < 0) return -1;
            resp->status_code = status;
            first_line = 0;
        } else {
            /* 解析 Content-Length */
            if (strncasecmp(line, "content-length:", 15) == 0) {
                const char *val = line + 15;
                while (*val == ' ') val++;
                resp->content_length = 0;
                while (*val >= '0' && *val <= '9') {
                    resp->content_length = resp->content_length * 10 + (*val - '0');
                    val++;
                }
            }
            /* Transfer-Encoding: chunked */
            else if (strncasecmp(line, "transfer-encoding:", 18) == 0) {
                if (strstr(line + 18, "chunked"))
                    resp->chunked = 1;
            }
            /* Location: (重定向) */
            else if (strncasecmp(line, "location:", 9) == 0) {
                resp->location_offset = line_start;
            }
            /* Accept-Ranges: bytes */
            else if (strncasecmp(line, "accept-ranges:", 14) == 0) {
                const char *val = line + 14;
                while (*val == ' ') val++;
                if (strncasecmp(val, "bytes", 5) == 0)
                    resp->accept_ranges = 1;
            }
        }
    }

    resp->headers_len = pos;
    return (pos < buf_sz) ? pos : -1;
}

/* ================================================================
 *  HTTP 完整请求 — 发送 + 接收响应头
 * ================================================================ */
static int http_request(int sock, const char *method,
                        const char *req_path, const char *host_header,
                        const char *extra_headers,
                        char *hdr_buf, int hdr_sz,
                        struct http_response *resp)
{
    if (send_http_request(sock, method, req_path, host_header, extra_headers) < 0)
        return -1;
    return recv_headers(sock, hdr_buf, hdr_sz, resp);
}

/* ================================================================
 *  下载数据体到文件
 * ================================================================ */

/* 从 sock 读取 chunked 数据并写入文件 */
static int read_chunked(int sock, int fd, long long *written)
{
    char buf[WGET_BUF_SIZE];
    *written = 0;

    for (;;) {
        /* 读 chunk 大小行 */
        char size_line[64];
        int n = read_line(sock, size_line, sizeof(size_line));
        if (n <= 0) break;

        long chunk_sz = 0;
        const char *p = size_line;
        while (*p) {
            char c = *p;
            if (c >= '0' && c <= '9')       chunk_sz = chunk_sz * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f')  chunk_sz = chunk_sz * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')  chunk_sz = chunk_sz * 16 + (c - 'A' + 10);
            else break;
            p++;
        }

        if (chunk_sz == 0) {
            /* 最终 chunk，跳过尾部 */
            read_line(sock, buf, sizeof(buf));
            break;
        }

        /* 读 chunk 数据 */
        long remaining = chunk_sz;
        while (remaining > 0) {
            int to_read = (remaining > (long)sizeof(buf)) ? (int)sizeof(buf) : (int)remaining;
            int r = (int)__read(sock, buf, to_read);
            if (r <= 0) return -1;
            __write(fd, buf, r);
            remaining -= r;
            *written += r;
        }

        /* 跳过 CRLF */
        read_line(sock, buf, sizeof(buf));
    }
    return 0;
}

/* 从 sock 读取指定长度数据并写入文件 */
static int read_fixed(int sock, int fd, long long len)
{
    char buf[WGET_BUF_SIZE];
    long long remaining = len;

    while (remaining > 0) {
        int to_read = (remaining > (long long)sizeof(buf)) ? (int)sizeof(buf) : (int)remaining;
        int r = (int)__read(sock, buf, to_read);
        if (r <= 0) return -1;
        __write(fd, buf, r);
        remaining -= r;
    }
    return 0;
}

/* 读取 body 直到连接关闭，写入文件 */
static int read_until_close(int sock, int fd, long long *written)
{
    char buf[WGET_BUF_SIZE];
    *written = 0;
    for (;;) {
        int r = (int)__read(sock, buf, sizeof(buf));
        if (r < 0) return -1;
        if (r == 0) break;
        __write(fd, buf, r);
        *written += r;
    }
    return 0;
}

/* ================================================================
 *  进度条显示
 * ================================================================ */

struct progress {
    volatile long long downloaded;  /* 已下载字节 */
    long long          total;       /* 总字节（-1 未知） */
    int                active;      /* 进度条是否激活 */
    volatile int       done_count;  /* 已完成的线程数 */
};

/* 显示单行进度条（用 \r 覆盖） */
static void show_progress_line(struct progress *p, int force)
{
    if (g_quiet) return;
    if (!p->active && !force) return;

    int width = get_term_width();
    if (width < 20) width = 20;
    width -= 2; /* 左右边距 */

    long long downloaded = p->downloaded;
    long long total = p->total;

    /* 构建输出字符串，然后一行 printf */
    char line[512];

    if (total > 0) {
        int pct = (int)(downloaded * 100 / total);
        /* 进度条宽度：最多 40 字符 */
        int bar_w = (width < 60) ? (width - 30) : 40;
        if (bar_w < 10) bar_w = 10;
        int filled = (int)(bar_w * downloaded / total);
        if (filled > bar_w) filled = bar_w;

        char bar[64];
        int bi = 0;
        bar[bi++] = '[';
        for (int i = 0; i < bar_w; i++) {
            if (i < filled) {
                if (i == bar_w - 1) bar[bi++] = '>';
                else if (i < filled - 1) bar[bi++] = '=';
                else bar[bi++] = '>';
            } else {
                bar[bi++] = ' ';
            }
        }
        bar[bi++] = ']';
        bar[bi] = '\0';

        char size_str[32], total_str[32];
        format_size(size_str, sizeof(size_str), downloaded);
        format_size(total_str, sizeof(total_str), total);

        snprintf(line, sizeof(line), "\r%s %3d%% %s/%s",
                 bar, pct, size_str, total_str);
    } else {
        /* 未知大小，只显示已下载量 */
        char size_str[32];
        format_size(size_str, sizeof(size_str), downloaded);
        snprintf(line, sizeof(line), "\r  %s  %ld bytes received", size_str, (long)downloaded);
    }

    /* 填充到宽度（避免终端换行） */
    int len = (int)strlen(line);
    if (len < width) {
        for (int i = len; i < width && i < (int)sizeof(line) - 1; i++)
            line[i] = ' ';
        line[width] = '\0';
    }

    __write(1, line, strlen(line));
}

/* ================================================================
 *  并行分块下载 — 每个线程下载一个 Range 区间
 * ================================================================ */

struct thread_arg {
    const char *host;
    int         port;
    const char *proxy_host;
    int         proxy_port;
    const char *req_path;       /* 请求路径（对代理可能是完整 URL） */
    const char *host_header;    /* Host 头 */
    const char *outfile;        /* 输出文件路径 */
    long long   start;          /* 范围起始 */
    long long   end;            /* 范围结束（包含），-1 表示到文件尾 */
    int         thread_id;
    int         num_threads;

    /* 共享进度 */
    struct progress *progress;
};

static void *download_thread(void *arg)
{
    struct thread_arg *ta = (struct thread_arg *)arg;
    int sock;
    char hdr_buf[WGET_HDR_BUF];
    struct http_response resp;
    char range_hdr[128];

    /* 构造 Range 头 */
    if (ta->end > 0)
        snprintf(range_hdr, sizeof(range_hdr),
            "Range: bytes=%ld-%ld\r\n", (long)ta->start, (long)ta->end);
    else
        snprintf(range_hdr, sizeof(range_hdr),
            "Range: bytes=%ld-\r\n", (long)ta->start);

    /* 连接 */
    sock = connect_target(ta->host, ta->port, ta->proxy_host, ta->proxy_port);
    if (sock < 0) {
        if (!g_quiet)
            printf("  [Thread %d] connect failed\n", ta->thread_id);
        atomic_fetch_add_u32((volatile uint32_t *)&ta->progress->done_count, 1);
        return (void*)(long)-1;
    }

    /* 发送请求 */
    if (send_http_request(sock, "GET", ta->req_path, ta->host_header, range_hdr) < 0) {
        __close(sock);
        atomic_fetch_add_u32((volatile uint32_t *)&ta->progress->done_count, 1);
        return (void*)(long)-1;
    }

    /* 接收头部 */
    if (recv_headers(sock, hdr_buf, sizeof(hdr_buf), &resp) < 0) {
        __close(sock);
        atomic_fetch_add_u32((volatile uint32_t *)&ta->progress->done_count, 1);
        return (void*)(long)-1;
    }

    if (resp.status_code != 206 && resp.status_code != 200) {
        if (!g_quiet)
            printf("  [Thread %d] unexpected status: %d\n", ta->thread_id, resp.status_code);
        __close(sock);
        atomic_fetch_add_u32((volatile uint32_t *)&ta->progress->done_count, 1);
        return (void*)(long)-1;
    }

    /* 打开输出文件（每个线程独立 fd，避免 lseek 竞争） */
    int fd = __openat(AT_FDCWD, ta->outfile, O_WRONLY, 0);
    if (fd < 0) {
        __close(sock);
        return (void*)(long)-1;
    }

    /* 定位到写入位置 */
    __lseek(fd, ta->start, SEEK_SET);

    /* 写数据体 */
    {
        long long written = 0;
        if (resp.chunked) {
            read_chunked(sock, fd, &written);
        } else if (resp.content_length >= 0) {
            read_fixed(sock, fd, resp.content_length);
            written = resp.content_length;
        } else {
            read_until_close(sock, fd, &written);
        }
        /* 原子更新共享进度 */
        if (written > 0)
            atomic_fetch_add_u64((volatile uint64_t *)&ta->progress->downloaded, (uint64_t)written);
    }

    __close(sock);
    __close(fd);
    atomic_fetch_add_u32((volatile uint32_t *)&ta->progress->done_count, 1);
    return (void*)(long)0;
}

/* ================================================================
 *  获取文件大小（resume 用）
 * ================================================================ */
static long long file_size(const char *path)
{
    struct stat st;
    if (tlibc_stat(path, &st) != 0) return -1;
    return st.st_size;
}

/* ================================================================
 *  main
 * ================================================================ */
int main(int argc, char *argv[])
{
    const char *outfile = NULL;
    int resume = 0;
    int parallel = 1;
    const char *proxy_url = NULL;
    int arg_idx;
    const char *url = NULL;

    /* 解析命令行参数 */
    for (arg_idx = 1; arg_idx < argc; arg_idx++) {
        const char *a = argv[arg_idx];
        if (a[0] != '-') {
            url = a;
            continue;
        }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(a, "-O") == 0) {
            if (++arg_idx >= argc) {
                printf("Error: -O requires an argument\n");
                return 1;
            }
            outfile = argv[arg_idx];
            continue;
        }
        if (strcmp(a, "-c") == 0) { resume = 1; continue; }
        if (strcmp(a, "-q") == 0) { g_quiet = 1; continue; }
        if (strcmp(a, "--parallel") == 0) {
            if (++arg_idx >= argc) {
                printf("Error: --parallel requires an argument\n");
                return 1;
            }
            parallel = (int)strtol(argv[arg_idx], NULL, 10);
            if (parallel < 1) parallel = 1;
            if (parallel > 16) parallel = 16;
            continue;
        }
        if (strcmp(a, "-x") == 0) {
            if (++arg_idx >= argc) {
                printf("Error: -x requires an argument\n");
                return 1;
            }
            proxy_url = argv[arg_idx];
            continue;
        }
        printf("Unknown option: %s\n", a);
        print_usage(argv[0]);
        return 1;
    }

    if (!url) {
        printf("Error: no URL specified\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ── 解析 URL ── */
    char host[512];
    int  port;
    char path[4096];

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        printf("Error: failed to parse URL '%s'\n", url);
        return 1;
    }

    /* ── 解析代理 URL ── */
    char proxy_host[256] = "";
    int  proxy_port = 0;
    char proxy_path_dummy[16]; /* 代理 URL 的 path 部分不用 */
    if (proxy_url) {
        if (parse_url(proxy_url, proxy_host, sizeof(proxy_host),
                       &proxy_port, proxy_path_dummy, sizeof(proxy_path_dummy)) != 0) {
            printf("Error: failed to parse proxy URL '%s'\n", proxy_url);
            return 1;
        }
    }

    /* ── 确定输出文件名 ── */
    char auto_outfile[512];
    if (!outfile) {
        /* 从 URL path 提取 basename */
        const char *last_slash = strrchr(path, '/');
        const char *base = last_slash ? last_slash + 1 : path;
        if (base[0] == '\0') base = "index.html";

        /* 如果 basename 仍为空或为 "." */
        if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
            base = "index.html";

        snprintf(auto_outfile, sizeof(auto_outfile), "%s", base);
        outfile = auto_outfile;
    }

    /* ── 根据代理模式决定请求路径 ── */
    const char *req_path;
    char full_url[4096];
    if (proxy_host[0]) {
        /* 代理模式下，请求行使用完整 URL */
        snprintf(full_url, sizeof(full_url), "http://%s:%d%s", host, port, path);
        req_path = full_url;
    } else {
        req_path = path;
    }

    /* ── 连接并发送 HEAD 请求，获取文件信息 ── */
    struct http_response head_resp;
    char hdr_buf[WGET_HDR_BUF];
    long long remote_size = -1;
    int supports_range = 0;

    int sock = connect_target(host, port, proxy_host, proxy_port);
    if (sock < 0) {
        printf("Error: cannot connect to %s:%d\n", host, port);
        return 1;
    }

    head_resp.status_code = 0;
    if (http_request(sock, "HEAD", req_path, host, NULL,
                     hdr_buf, sizeof(hdr_buf), &head_resp) >= 0) {
        remote_size = head_resp.content_length;
        supports_range = head_resp.accept_ranges;
    }
    __close(sock);

    /* ── 断点续传：检查本地文件 ── */
    long long local_size = 0;
    if (resume) {
        local_size = file_size(outfile);
        if (local_size < 0) {
            /* 文件不存在，从头下载 */
            local_size = 0;
            if (!g_quiet)
                printf("No existing file, starting from beginning.\n");
        } else if (remote_size > 0 && local_size >= remote_size) {
            if (!g_quiet)
                printf("File already fully downloaded.\n");
            return 0;
        } else {
            if (!g_quiet)
                printf("Resuming from %ld bytes\n", (long)local_size);
        }
    }

    /* ── 打开/创建输出文件 ── */
    int file_flags = O_WRONLY | O_CREAT;
    if (!resume || local_size == 0)
        file_flags |= O_TRUNC;

    int fd = __openat(AT_FDCWD, outfile, file_flags, 0644);
    if (fd < 0) {
        printf("Error: cannot open '%s' for writing\n", outfile);
        return 1;
    }

    if (resume && local_size > 0)
        __lseek(fd, local_size, SEEK_SET);

    /* ── 确定并行下载策略 ── */
    int use_parallel = (parallel > 1 && remote_size > 1024 * 1024 && supports_range);

    if (use_parallel) {
        /* 并行下载：先预分配文件 */
        if (resume) {
            if (remote_size > local_size)
                __ftruncate(fd, remote_size);
        } else {
            __ftruncate(fd, remote_size);
        }
        __close(fd);

        /* 分块 */
        int nthreads = parallel;
        long long chunk_size = (remote_size - local_size) / nthreads;
        if (chunk_size < 1024 * 256) { /* 每块至少 256KB */
            nthreads = (int)((remote_size - local_size) / (1024 * 256));
            if (nthreads < 1) nthreads = 1;
            if (nthreads > parallel) nthreads = parallel;
            chunk_size = (remote_size - local_size) / nthreads;
        }

        struct progress prog;
        prog.downloaded = local_size;
        prog.total = remote_size;
        prog.active = 1;

        pthread_t threads[16];
        struct thread_arg args[16];
        int nt = (nthreads > 16) ? 16 : nthreads;

        if (!g_quiet)
            printf("Downloading '%s' with %d threads...\n", outfile, nt);

        for (int i = 0; i < nt; i++) {
            args[i].host = host;
            args[i].port = port;
            args[i].proxy_host = proxy_host[0] ? proxy_host : NULL;
            args[i].proxy_port = proxy_port;
            args[i].req_path = req_path;
            args[i].host_header = host;
            args[i].outfile = outfile;
            args[i].start = local_size + i * chunk_size;
            if (i == nt - 1)
                args[i].end = remote_size - 1;
            else
                args[i].end = args[i].start + chunk_size - 1;
            args[i].thread_id = i;
            args[i].num_threads = nt;
            args[i].progress = &prog;
        }

        /* 创建线程 */
        prog.done_count = 0;
        for (int i = 0; i < nt; i++)
            pthread_create(&threads[i], NULL, download_thread, &args[i]);

        /* 等待线程 + 更新进度 */
        while (prog.done_count < nt) {
            show_progress_line(&prog, 0);
            tlibc_usleep(250000); /* 250ms */
        }

        /* 收尾 */
        for (int i = 0; i < nt; i++)
            pthread_join(threads[i], NULL);

        prog.downloaded = remote_size;
        show_progress_line(&prog, 1);
        if (!g_quiet) printf("\nDownload complete: %s\n", outfile);

    } else {
        /* ── 单线程下载 ── */
        /* 构造 Range 头（断点续传） */
        char extra_hdr[128] = "";
        if (resume && local_size > 0) {
            snprintf(extra_hdr, sizeof(extra_hdr),
                     "Range: bytes=%ld-\r\n", (long)local_size);
        }

        /* 连接并发送 GET */
        sock = connect_target(host, port, proxy_host, proxy_port);
        if (sock < 0) {
            printf("Error: cannot connect to %s:%d\n", host, port);
            __close(fd);
            return 1;
        }

        if (!g_quiet) {
            const char *range_hint = (resume && local_size > 0) ? " (resume)" : "";
            printf("Downloading '%s'...%s\n", outfile, range_hint);
        }

        struct http_response resp;
        if (http_request(sock, "GET", req_path, host,
                         extra_hdr[0] ? extra_hdr : NULL,
                         hdr_buf, sizeof(hdr_buf), &resp) < 0) {
            printf("Error: HTTP request failed\n");
            __close(sock);
            __close(fd);
            return 1;
        }

        /* 检查状态码（断点续传期望 206） */
        if (resume && local_size > 0 && resp.status_code != 206) {
            if (resp.status_code == 200) {
                /* 服务器不支持 Range，从头下载 */
                if (!g_quiet)
                    printf("Server does not support resume, restarting.\n");
                __lseek(fd, 0, SEEK_SET);
                __ftruncate(fd, 0);
                local_size = 0;
            }
        }

        /* 进度条初始化 */
        struct progress prog;
        prog.downloaded = local_size;
        if (resp.content_length >= 0)
            prog.total = local_size + resp.content_length;
        else if (remote_size > 0)
            prog.total = remote_size;
        else
            prog.total = -1;
        prog.active = (prog.total > 0 || !g_quiet);

        /* 写数据体 */
        if (resp.chunked) {
            long long written = 0;
            read_chunked(sock, fd, &written);
            prog.downloaded += written;
        } else if (resp.content_length >= 0) {
            read_fixed(sock, fd, resp.content_length);
            prog.downloaded = local_size + resp.content_length;
        } else {
            long long written = 0;
            read_until_close(sock, fd, &written);
            prog.downloaded = local_size + written;
        }

        show_progress_line(&prog, 1);
        __close(sock);

        if (!g_quiet) {
            char sz[32];
            format_size(sz, sizeof(sz), prog.downloaded);
            printf("\nDownload complete: %s (%s)\n", outfile, sz);
        }
    }

    __close(fd);
    return 0;
}
