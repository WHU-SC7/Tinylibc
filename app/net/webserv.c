/* SPDX-License-Identifier: MIT
 *
 * webserv — 轻量 HTTP/1.1 文件服务器
 *
 * 在浏览器中浏览和下载文件，支持目录导航。
 *
 * 用法:
 *   webserv [port] [root_dir]
 *   默认 port=8080, root_dir=当前工作目录
 *
 * 限制（文档化）:
 * - 仅支持 GET 方法
 * - 无 keep-alive (Connection: close)
 * - 无 Range/ETag/If-Modified-Since
 * - 无 index.html 自动索引
 * - 单次 recv() 读取请求，2048 字节上限
 * - 修改时间显示为 Unix 时间戳
 */

#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"

#define WEBSERV_PORT     8080
#define REQ_BUF_SIZE     2048
#define FILE_CHUNK_SIZE  4096
#define PATH_MAX         1024

/* ------------------------------------------------------------------ */
/*  Structures                                                        */
/* ------------------------------------------------------------------ */

struct webserv_thread_arg {
    int client_fd;
    struct sockaddr_in client_addr;
    char root_dir[PATH_MAX];
};

struct http_request {
    char method[16];
    char path[PATH_MAX];
};

/* ------------------------------------------------------------------ */
/*  MIME Type Mapping                                                 */
/* ------------------------------------------------------------------ */

static const struct {
    const char *ext;
    const char *mime;
} mime_table[] = {
    {".html", "text/html; charset=utf-8"},
    {".htm",  "text/html; charset=utf-8"},
    {".css",  "text/css; charset=utf-8"},
    {".js",   "application/javascript"},
    {".json", "application/json"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".svg",  "image/svg+xml"},
    {".ico",  "image/x-icon"},
    {".txt",  "text/plain; charset=utf-8"},
    {".md",   "text/markdown; charset=utf-8"},
    {".pdf",  "application/pdf"},
    {".zip",  "application/zip"},
    {".gz",   "application/gzip"},
    {".tar",  "application/x-tar"},
    {".mp3",  "audio/mpeg"},
    {".mp4",  "video/mp4"},
    {".xml",  "application/xml"},
    {".csv",  "text/csv; charset=utf-8"},
    {".sh",   "text/plain; charset=utf-8"},
    {".conf", "text/plain; charset=utf-8"},
    {".log",  "text/plain; charset=utf-8"},
    {".c",    "text/plain; charset=utf-8"},
    {".h",    "text/plain; charset=utf-8"},
    {".S",    "text/plain; charset=utf-8"},
    {".ld",   "text/plain; charset=utf-8"},
    {NULL, NULL}
};

static const char *
get_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "application/octet-stream";
    for (int i = 0; mime_table[i].ext != NULL; i++) {
        if (strcmp(dot, mime_table[i].ext) == 0)
            return mime_table[i].mime;
    }
    return "application/octet-stream";
}

/* ------------------------------------------------------------------ */
/*  URL Encoding / Decoding                                           */
/* ------------------------------------------------------------------ */

static void
url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            int val = 0;
            char hi = src[1], lo = src[2];
            if (hi >= '0' && hi <= '9')      val += (hi - '0') * 16;
            else if (hi >= 'a' && hi <= 'f') val += (hi - 'a' + 10) * 16;
            else if (hi >= 'A' && hi <= 'F') val += (hi - 'A' + 10) * 16;
            if (lo >= '0' && lo <= '9')      val += (lo - '0');
            else if (lo >= 'a' && lo <= 'f') val += (lo - 'a' + 10);
            else if (lo >= 'A' && lo <= 'F') val += (lo - 'A' + 10);
            dst[i++] = (char)val;
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void
strip_query_fragment(char *path)
{
    for (char *p = path; *p; p++) {
        if (*p == '?' || *p == '#') {
            *p = '\0';
            break;
        }
    }
}

static void
url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            dst[i++] = c;
        } else {
            if (i + 3 >= dst_size - 1)
                break;
            dst[i++] = '%';
            dst[i++] = hex[c >> 4];
            dst[i++] = hex[c & 0x0f];
        }
        src++;
    }
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Path Safety                                                        */
/* ------------------------------------------------------------------ */

/* 检查归一化后的路径是否仍在 root_dir 内 */
static int
check_path_safe(const char *resolved, const char *root_dir)
{
    size_t root_len = strlen(root_dir);
    return (strncmp(resolved, root_dir, root_len) == 0 &&
            (resolved[root_len] == '\0' || resolved[root_len] == '/'));
}

/* ------------------------------------------------------------------ */
/*  HTTP Request Parsing                                               */
/* ------------------------------------------------------------------ */

/* 解析 "GET /path HTTP/1.1\r\n..." 返回 method 和 path */
static int
parse_http_request(const char *raw, int raw_len, struct http_request *req)
{
    const char *line_end = NULL;

    /* 找第一行结尾 */
    for (int i = 0; i < raw_len && raw[i]; i++) {
        if (raw[i] == '\r' && i + 1 < raw_len && raw[i + 1] == '\n') {
            line_end = raw + i;
            break;
        }
        if (raw[i] == '\n') {
            line_end = raw + i;
            break;
        }
    }
    if (!line_end) return -1;

    /* 跳过前导空白 */
    const char *tok = raw;
    while (*tok == ' ' || *tok == '\t') tok++;

    /* Method */
    const char *sp = strchr(tok, ' ');
    if (!sp || (sp - tok) >= (int)sizeof(req->method)) return -1;
    memcpy(req->method, tok, sp - tok);
    req->method[sp - tok] = '\0';
    tok = sp + 1;

    while (*tok == ' ') tok++; /* 跳过 Method 和 Path 之间的空格 */

    /* Path */
    const char *sp2 = strchr(tok, ' ');
    if (!sp2 || (sp2 - tok) >= (int)sizeof(req->path)) return -1;
    memcpy(req->path, tok, sp2 - tok);
    req->path[sp2 - tok] = '\0';

    return 0;
}

/* ------------------------------------------------------------------ */
/*  HTTP Response Helpers                                              */
/* ------------------------------------------------------------------ */

static void
send_error(int fd, int code, const char *text, const char *msg)
{
    fdprintf(fd,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<title>%d %s</title>"
        "<style>"
        "body{font-family:sans-serif;margin:40px;background:#fff;color:#333}"
        "h1{font-size:1.5em;border-bottom:2px solid #e00;padding-bottom:8px}"
        ".code{font-size:3em;color:#c00;font-weight:bold}"
        "p{margin-top:20px;font-size:1.1em}"
        "hr{margin-top:30px}"
        "</style></head><body>"
        "<div class=\"code\">%d %s</div>"
        "<h1>%s</h1><hr>"
        "<em>Tinylibc HTTP File Server</em>"
        "</body></html>\n",
        code, text,
        code, text,
        code, text,
        msg);
}

static void
send_redirect(int fd, const char *location)
{
    fdprintf(fd,
        "HTTP/1.1 301 Moved Permanently\r\n"
        "Location: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);
}

/* ------------------------------------------------------------------ */
/*  File Serving                                                       */
/* ------------------------------------------------------------------ */

static void
serve_file(int fd, const char *abs_path)
{
    int file_fd = openat(AT_FDCWD, abs_path, O_RDONLY, 0);
    if (file_fd < 0) {
        send_error(fd, 500, "Internal Server Error",
                   "Could not open the requested file");
        return;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    if (fstat(file_fd, &st) < 0) {
        send_error(fd, 500, "Internal Server Error",
                   "Could not stat the requested file");
        close(file_fd);
        return;
    }

    const char *mime = get_mime_type(abs_path);

    fdprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "Accept-Ranges: none\r\n"
        "\r\n",
        mime, (long)st.st_size);

    /* 以块发送文件体 */
    {
        char chunk[FILE_CHUNK_SIZE];
        int n;
        while ((n = read(file_fd, chunk, sizeof(chunk))) > 0)
            write(fd, chunk, n);
    }

    close(file_fd);
}

/* ------------------------------------------------------------------ */
/*  Directory Listing                                                  */
/* ------------------------------------------------------------------ */

static void
serve_directory(int fd, const char *abs_path, const char *url_path)
{
    /*
     * url_path 是用于构造 HTML 链接的 URL 路径。
     * 根目录时为 ""，子目录时如 "/subdir"（无尾部斜杠）。
     */

    int dir_fd = openat(AT_FDCWD, abs_path, O_RDONLY | O_DIRECTORY, 0644);
    if (dir_fd < 0) {
        send_error(fd, 500, "Internal Server Error",
                   "Could not open directory");
        return;
    }

    char *ls_buf = mmap(0, TLIBC_BUF_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ls_buf == MAP_FAILED) {
        close(dir_fd);
        send_error(fd, 500, "Internal Server Error",
                   "Out of memory");
        return;
    }

    memset(ls_buf, 0, TLIBC_BUF_SIZE);
    int ret = getdents64(dir_fd,
                         (struct linux_dirent64 *)ls_buf,
                         TLIBC_BUF_SIZE);
    close(dir_fd);

    if (ret < 0) {
        munmap(ls_buf, TLIBC_BUF_SIZE);
        send_error(fd, 500, "Internal Server Error",
                   "Failed to read directory");
        return;
    }

    /* ---- HTTP 头 ---- */
    fdprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n");

    /* ---- HTML 头 ---- */
    const char *display_name = url_path[0] ? url_path : "/";
    fdprintf(fd,
        "<!DOCTYPE html>\n"
        "<html><head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<title>Index of %s</title>\n"
        "<style>\n"
        "body{font-family:monospace;margin:20px;background:#fff;color:#333}\n"
        "h1{font-size:1.3em;border-bottom:1px solid #ccc;padding-bottom:6px}\n"
        "table{border-collapse:collapse;width:100%%}\n"
        "th{text-align:left;padding:4px 16px 4px 0;border-bottom:1px solid #ddd}\n"
        "td{padding:4px 16px 4px 0;white-space:nowrap}\n"
        "tr:hover{background:#f5f5f5}\n"
        "a{color:#0366d6;text-decoration:none}\n"
        "a:hover{text-decoration:underline}\n"
        "a.dir{font-weight:bold}\n"
        ".size{text-align:right;color:#555;padding-right:20px}\n"
        ".date{color:#888;font-size:0.9em}\n"
        "</style>\n"
        "</head><body>\n"
        "<h1>Index of %s</h1>\n"
        "<hr>\n"
        "<table>\n"
        "<tr><th>Name</th><th>Size</th><th>Last modified</th></tr>\n",
        display_name, display_name);

    /* 父目录链接（非根目录） */
    if (url_path[0] != '\0') {
        fdprintf(fd,
            "<tr><td><a href=\"../\">../</a></td>"
            "<td class=\"size\">-</td>"
            "<td class=\"date\"></td></tr>\n");
    }

    /* ---- 遍历目录条目 ---- */
    {
        struct linux_dirent64 *data = (struct linux_dirent64 *)ls_buf;
        while (data->d_off != 0) {
            char *name = data->d_name;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
                continue;
            }

            /* stat 获取文件信息 */
            char entry_path[PATH_MAX];
            snprintf(entry_path, sizeof(entry_path),
                     "%s/%s", abs_path, name);

            struct stat st;
            memset(&st, 0, sizeof(st));
            int is_dir = 0;
            long file_size = 0;
            long mod_time = 0;

            if (stat(entry_path, &st) == 0) {
                is_dir   = S_ISDIR(st.st_mode);
                file_size = (long)st.st_size;
                mod_time  = (long)st.st_mtim.tv_sec;
            }

            /* URL 编码 href */
            char encoded[512];
            url_encode(name, encoded, sizeof(encoded));

            /* 格式化大小 */
            char size_str[32];
            if (is_dir) {
                size_str[0] = '-';
                size_str[1] = '\0';
            } else if (file_size < 1024) {
                snprintf(size_str, sizeof(size_str), "%ld B", file_size);
            } else if (file_size < 1024 * 1024) {
                snprintf(size_str, sizeof(size_str), "%ld KB",
                         file_size / 1024);
            } else {
                snprintf(size_str, sizeof(size_str), "%ld MB",
                         file_size / (1024 * 1024));
            }

            if (is_dir) {
                fdprintf(fd,
                    "<tr><td><a class=\"dir\" href=\"%s/%s/\">%s/</a></td>"
                    "<td class=\"size\">%s</td>"
                    "<td class=\"date\">%ld</td></tr>\n",
                    url_path, encoded, name, size_str, mod_time);
            } else {
                fdprintf(fd,
                    "<tr><td><a href=\"%s/%s\">%s</a></td>"
                    "<td class=\"size\">%s</td>"
                    "<td class=\"date\">%ld</td></tr>\n",
                    url_path, encoded, name, size_str, mod_time);
            }

            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }

    /* ---- HTML 尾 ---- */
    fdprintf(fd,
        "</table>\n<hr>\n"
        "<em>Tinylibc HTTP File Server</em>\n"
        "</body></html>\n");

    munmap(ls_buf, TLIBC_BUF_SIZE);
}

/* ------------------------------------------------------------------ */
/*  Per-Connection Thread Entry                                        */
/* ------------------------------------------------------------------ */

static void *
webserv_thread_entry(void *arg)
{
    struct webserv_thread_arg *targ = (struct webserv_thread_arg *)arg;
    int fd = targ->client_fd;

    /* ---- 接收请求 ---- */
    char buf[REQ_BUF_SIZE];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        goto out;
    buf[n] = '\0';

    /* ---- 解析 HTTP 请求行 ---- */
    struct http_request req;
    if (parse_http_request(buf, n, &req) < 0) {
        send_error(fd, 400, "Bad Request",
                   "Malformed HTTP request");
        goto out;
    }

    /* 只支持 GET */
    if (strcmp(req.method, "GET") != 0) {
        send_error(fd, 405, "Method Not Allowed",
                   "Only GET is supported");
        goto out;
    }

    /* ---- URL 解码 + 清理 ---- */
    char decoded[PATH_MAX];
    url_decode(decoded, req.path, sizeof(decoded));
    strip_query_fragment(decoded);

    /* ---- 路径安全处理 ---- */
    char full_path[PATH_MAX];
    if (strcmp(decoded, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "%s", targ->root_dir);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s",
                 targ->root_dir, decoded);
    }

    char normalized[PATH_MAX];
    tlibc_cal_absolute_path(full_path, NULL,
                            normalized, sizeof(normalized));

    if (!check_path_safe(normalized, targ->root_dir)) {
        send_error(fd, 403, "Forbidden",
                   "Directory traversal is not allowed");
        goto out;
    }

    /* ---- 检查路径是否存在 ---- */
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(normalized, &st) < 0) {
        send_error(fd, 404, "Not Found",
                   "The requested resource was not found on this server");
        goto out;
    }

    /*
     * 为 HTML 链接构造显示路径。
     * 根目录 → ""（避免 href="//name"），子目录 → "/subdir"（无尾部斜杠）
     */
    char display_path[PATH_MAX];
    if (strcmp(decoded, "/") == 0) {
        display_path[0] = '\0';
    } else {
        strncpy(display_path, decoded, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
        int dlen = strlen(display_path);
        if (dlen > 0 && display_path[dlen - 1] == '/')
            display_path[dlen - 1] = '\0';
    }

    /* 目录 URL 缺少尾部斜杠时重定向（保证浏览器相对链接正确） */
    if (S_ISDIR(st.st_mode)) {
        int dlen = strlen(decoded);
        if (dlen == 0 || decoded[dlen - 1] != '/') {
            char redirect[PATH_MAX];
            snprintf(redirect, sizeof(redirect), "%s/", decoded);
            send_redirect(fd, redirect);
            goto out;
        }
    }

    /* ---- 响应 ---- */
    if (S_ISDIR(st.st_mode))
        serve_directory(fd, normalized, display_path);
    else if (S_ISREG(st.st_mode))
        serve_file(fd, normalized);
    else
        send_error(fd, 403, "Forbidden",
                   "Access to this resource type is forbidden");

out:
    close(fd);
    munmap(targ, sizeof(struct webserv_thread_arg));
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int port = WEBSERV_PORT;
    char root_dir[PATH_MAX];

    /* 默认根目录：当前工作目录 */
    {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            printf("getcwd() failed\n");
            return 1;
        }
        strncpy(root_dir, cwd, sizeof(root_dir) - 1);
        root_dir[sizeof(root_dir) - 1] = '\0';
    }

    /* 解析命令行参数 */
    if (argc >= 2) {
        int is_digits = 1;
        for (char *p = argv[1]; *p; p++) {
            if (*p < '0' || *p > '9') { is_digits = 0; break; }
        }

        if (is_digits) {
            port = (int)tlibc_strtoul(argv[1]);
            if (argc >= 3) {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)))
                    tlibc_cal_absolute_path(argv[2], cwd,
                                            root_dir, sizeof(root_dir));
            }
        } else {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                char abs_path[PATH_MAX];
                tlibc_cal_absolute_path(argv[1], cwd,
                                        abs_path, sizeof(abs_path));
                if (tlibc_is_path_dir(abs_path) == 1) {
                    strncpy(root_dir, abs_path, sizeof(root_dir) - 1);
                    root_dir[sizeof(root_dir) - 1] = '\0';
                } else {
                    printf("Error: '%s' is not a valid directory\n",
                           argv[1]);
                    return 1;
                }
            }
        }
    }

    if (tlibc_is_path_dir(root_dir) != 1) {
        printf("Error: root directory '%s' is not valid\n", root_dir);
        return 1;
    }

    printf("Tinylibc HTTP File Server\n");
    printf("Serving: %s\n", root_dir);
    printf("Listening on port %d\n\n", port);

    /* ---- socket ---- */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("socket() failed\n");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        printf("setsockopt() failed\n");
        close(server_fd);
        return 1;
    }

    /* ---- bind ---- */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = tlibc_htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        printf("bind() failed\n");
        close(server_fd);
        return 1;
    }

    /* ---- listen ---- */
    if (listen(server_fd, 5) < 0) {
        printf("listen() failed\n");
        close(server_fd);
        return 1;
    }

    printf("Server started. Press Ctrl+C to stop.\n\n");

    /* ---- accept 主循环 ---- */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) {
            printf("accept() failed\n");
            continue;
        }

        printf("Connection from %s:%d\n",
               tlibc_inet_ntoa(client_addr.sin_addr),
               tlibc_ntohs(client_addr.sin_port));

        struct webserv_thread_arg *targ =
            mmap(0, sizeof(struct webserv_thread_arg),
                 PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        targ->client_fd = client_fd;
        targ->client_addr = client_addr;
        strncpy(targ->root_dir, root_dir,
                sizeof(targ->root_dir) - 1);
        targ->root_dir[sizeof(targ->root_dir) - 1] = '\0';

        pthread_t thread;
        pthread_create(&thread, NULL,
                       webserv_thread_entry, targ);
    }

    close(server_fd);
    return 0;
}
