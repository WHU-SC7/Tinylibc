/* SPDX-License-Identifier: MIT
 *
 * webserv — 轻量 HTTP/1.1 文件服务器
 *
 * 在浏览器中浏览、下载和上传文件，支持目录导航。
 *
 * 用法:
 *   webserv [port] [root_dir]
 *   默认 port=8080, root_dir=$HOME/tlibc_www（不存在则自动创建）
 *
 * 功能:
 * - GET: 文件下载 + 目录列表（含上传表单）
 * - POST: 通过 multipart/form-data 上传文件
 *
 * 限制（文档化）:
 * - 无 keep-alive (Connection: close)
 * - 无 Range/ETag/If-Modified-Since
 * - 无 index.html 自动索引
 * - 单次 recv() 读取头部，16384 字节上限
 * - 修改时间显示为 Unix 时间戳
 * - 上传大小上限 32 MiB
 */

#include "core.h"
#include "tlibc_print.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"


#define WEBSERV_PORT     8080
#define HEADER_BUF_SIZE  16384
#define FILE_CHUNK_SIZE  4096
#define PATH_MAX         1024
#define MAX_UPLOAD_SIZE  (32 * 1024 * 1024)  /* 32 MiB */
#define MAX_FILENAME     255
#define BOUNDARY_MAX     128
#define TLIBC_WWW_DIR    "tlibc_www"

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
    long content_length;        /* -1 表示未设置 */
    char content_type[256];     /* 空串表示未设置 */
    char boundary[128];         /* 仅 multipart 时有效 */
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
/*  HTTP Header Parsing                                                */
/* ------------------------------------------------------------------ */

static int
parse_http_headers(const char *raw, int raw_len,
                   struct http_request *req,
                   int *body_offset)
{
    *body_offset = 0;

    /* 跳过第一行（请求行） */
    int i = 0;
    while (i < raw_len && raw[i]) {
        if (raw[i] == '\r' && i + 1 < raw_len && raw[i + 1] == '\n') {
            i += 2;
            break;
        }
        if (raw[i] == '\n') {
            i += 1;
            break;
        }
        i++;
    }
    if (i >= raw_len || !raw[i])
        return 0;   /* 只有请求行，没有头部，也没 body */

    /* 解析头部行 */
    while (i < raw_len && raw[i]) {
        /* 检测空行（头部结束） */
        if (raw[i] == '\r' && i + 1 < raw_len && raw[i + 1] == '\n') {
            *body_offset = i + 2;
            return 0;
        }
        if (raw[i] == '\n') {
            *body_offset = i + 1;
            return 0;
        }

        /* 读取一行到 line_buf */
        char line[512];
        int li = 0;
        while (i < raw_len && raw[i] && li < (int)sizeof(line) - 1) {
            if (raw[i] == '\r' && i + 1 < raw_len && raw[i + 1] == '\n') {
                i += 2;
                break;
            }
            if (raw[i] == '\n') {
                i += 1;
                break;
            }
            line[li++] = raw[i++];
        }
        line[li] = '\0';

        /* 跳过空白行（如头部后的空行） */
        if (line[0] == '\0') {
            *body_offset = i;
            return 0;
        }

        /* 按 ": " 分割头名字和值 */
        char *colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';
        char *name = line;
        char *value = colon + 1;
        while (*value == ' ') value++;

        /* Content-Length */
        if (strcmp(name, "Content-Length") == 0 ||
            strcmp(name, "content-length") == 0) {
            req->content_length = 0;
            while (*value >= '0' && *value <= '9') {
                req->content_length = req->content_length * 10 + (*value - '0');
                value++;
            }
        }
        /* Content-Type */
        else if (strcmp(name, "Content-Type") == 0 ||
                 strcmp(name, "content-type") == 0) {
            strncpy(req->content_type, value, sizeof(req->content_type) - 1);
            req->content_type[sizeof(req->content_type) - 1] = '\0';

            /* 如果是 multipart/form-data，提取 boundary */
            if (strstr(value, "multipart/form-data")) {
                char *b = strstr(value, "boundary=");
                if (b) {
                    b += 9; /* skip "boundary=" */
                    /* 跳过前导空白 */
                    while (*b == ' ') b++;
                    int qi = 0;
                    int in_quote = (*b == '"');
                    if (in_quote) b++;
                    while (*b && *b != ';' && qi < (int)sizeof(req->boundary) - 1) {
                        if (in_quote && *b == '"') break;
                        req->boundary[qi++] = *b++;
                    }
                    req->boundary[qi] = '\0';
                }
            }
        }
    }

    return 0;   /* 没有找到空行（可能没有 body） */
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
/*  Upload Response Helper                                            */
/* ------------------------------------------------------------------ */

static void
send_upload_response(int fd, int saved_count,
                     char (*saved_names)[MAX_FILENAME+1],
                     long *saved_sizes,
                     const char *url_path)
{
    fdprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<title>Upload Result</title>"
        "<style>"
        "body{font-family:sans-serif;margin:40px;background:#fff;color:#333}"
        "h1{font-size:1.5em;border-bottom:2px solid #090;padding-bottom:8px}"
        "p{margin-top:16px;font-size:1.1em}"
        "a{color:#0366d6;text-decoration:none}"
        "a:hover{text-decoration:underline}"
        "ul{list-style:none;padding:0}"
        "li{padding:6px 10px;margin:4px 0;background:#f5f5f5;border-radius:3px}"
        ".size{color:#888;font-size:0.9em;margin-left:12px}"
        "hr{margin-top:30px}"
        "</style></head><body>");

    if (saved_count > 0) {
        fdprintf(fd, "<h1>✓ Upload successful</h1>\n<p>%d file(s) uploaded:</p>\n<ul>\n",
                 saved_count);
        for (int i = 0; i < saved_count; i++) {
            char size_str[32];
            if (saved_sizes[i] < 1024)
                snprintf(size_str, sizeof(size_str), "%ld B", saved_sizes[i]);
            else if (saved_sizes[i] < 1024 * 1024)
                snprintf(size_str, sizeof(size_str), "%ld KB", saved_sizes[i] / 1024);
            else
                snprintf(size_str, sizeof(size_str), "%ld MB",
                         saved_sizes[i] / (1024 * 1024));
            fdprintf(fd, "<li>%s <span class=\"size\">%s</span></li>\n",
                     saved_names[i], size_str);
        }
        fdprintf(fd, "</ul>\n");
    } else {
        fdprintf(fd,
            "<h1>Upload</h1>\n"
            "<p>No files were uploaded. The form may be missing a file field.</p>\n");
    }

    /* "返回目录" 链接 */
    if (url_path[0] == '\0')
        fdprintf(fd, "<hr>\n<a href=\"/\">&larr; Back to directory</a>\n");
    else
        fdprintf(fd, "<hr>\n<a href=\"%s/\">&larr; Back to directory</a>\n", url_path);
    fdprintf(fd, "</body></html>\n");
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
        ".upload-form{background:#f8f8f8;border:1px solid #ddd;"
        "border-radius:4px;padding:10px;margin-bottom:16px}\n"
        ".upload-form input[type=submit]{background:#0366d6;color:#fff;"
        "border:0;border-radius:3px;cursor:pointer;padding:4px 14px}\n"
        "</style>\n"
        "</head><body>\n"
        "<h1>Index of %s</h1>\n"
        "<hr>\n"
        "<form action=\".\" method=\"post\" enctype=\"multipart/form-data\" "
        "class=\"upload-form\">\n"
        "<strong>Upload file:</strong>\n"
        "<input type=\"file\" name=\"file\" required "
        "style=\"margin:0 8px;padding:4px\">\n"
        "<input type=\"submit\" value=\"Upload\">\n"
        "</form>\n"
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

/* 前向声明（handle_post_request 在该函数之后定义） */
static void
handle_post_request(int fd, char *buf, int buf_len,
                    struct http_request *req, int body_offset,
                    struct webserv_thread_arg *targ);

/* ------------------------------------------------------------------ */
/*  Per-Connection Thread Entry                                        */
/* ------------------------------------------------------------------ */

static void *
webserv_thread_entry(void *arg)
{
    struct webserv_thread_arg *targ = (struct webserv_thread_arg *)arg;
    int fd = targ->client_fd;

    /* ---- 接收请求 ---- */
    char buf[HEADER_BUF_SIZE];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        goto out;
    buf[n] = '\0';

    /* ---- 解析 HTTP 请求行 ---- */
    struct http_request req;
    memset(&req, 0, sizeof(req));
    req.content_length = -1;

    if (parse_http_request(buf, n, &req) < 0) {
        send_error(fd, 400, "Bad Request",
                   "Malformed HTTP request");
        goto out;
    }

    /* ---- 解析 HTTP 头部 ---- */
    int body_offset = 0;
    parse_http_headers(buf, n, &req, &body_offset);

    /* ---- 方法分发 ---- */
    if (strcmp(req.method, "GET") == 0) {
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

    } else if (strcmp(req.method, "POST") == 0) {
        handle_post_request(fd, buf, n, &req, body_offset, targ);
    } else {
        send_error(fd, 405, "Method Not Allowed",
                   "Only GET and POST are supported");
    }

out:
    close(fd);
    munmap(targ, sizeof(struct webserv_thread_arg));
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  POST Upload Handling                                               */
/* ------------------------------------------------------------------ */

static int
recv_request_body(int fd, const char *initial_buf, int initial_len,
                  int body_offset, long content_length,
                  char **out_body)
{
    if (content_length <= 0) {
        *out_body = NULL;
        return 0;
    }

    *out_body = mmap(0, content_length + 1,
                     PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (*out_body == MAP_FAILED) {
        *out_body = NULL;
        return -1;
    }

    /* 拷贝 initial_buf 中已收到的 body 数据 */
    long already = initial_len - body_offset;
    if (already > 0) {
        if (already > content_length)
            already = content_length;
        memcpy(*out_body, initial_buf + body_offset, already);
    }

    /* 继续 recv 直到收满 */
    long total = already;
    while (total < content_length) {
        int n = recv(fd, *out_body + total, content_length - total, 0);
        if (n <= 0) {
            munmap(*out_body, content_length + 1);
            *out_body = NULL;
            return -1;
        }
        total += n;
    }

    (*out_body)[content_length] = '\0';
    return 0;
}

static void
sanitize_filename(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        unsigned char c = (unsigned char)*src;
        /* 去除路径分隔符和控制字符 */
        if (c == '/' || c == '\\' || c < 32) {
            src++;
            continue;
        }
        /* 去除开头的 '.'（防止隐藏文件及 '..' 穿越） */
        if (i == 0 && c == '.') {
            src++;
            continue;
        }
        dst[i++] = c;
        src++;
    }
    dst[i] = '\0';

    /* 回退：空结果或危险结果 */
    if (i == 0 || strcmp(dst, ".") == 0 || strcmp(dst, "..") == 0)
        snprintf(dst, dst_size, "unnamed");
}

static int
save_file_part(const char *content, long content_len,
               const char *safe_name, const char *upload_dir)
{
    char abs_path[PATH_MAX];
    snprintf(abs_path, sizeof(abs_path), "%s/%s", upload_dir, safe_name);

    char normalized[PATH_MAX];
    tlibc_cal_absolute_path(abs_path, NULL, normalized, sizeof(normalized));

    /* 二次验证路径安全 */
    if (!check_path_safe(normalized, upload_dir)) {
        printf("Upload blocked: path traversal detected: %s\n", safe_name);
        return -1;
    }

    int file_fd = openat(AT_FDCWD, normalized,
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        printf("Failed to create upload file: %s\n", normalized);
        return -1;
    }

    long written = 0;
    while (written < content_len) {
        int n = write(file_fd, content + written, content_len - written);
        if (n < 0) {
            close(file_fd);
            return -1;
        }
        written += n;
    }

    close(file_fd);
    return 0;
}

static int
parse_multipart_upload(const char *body, long body_len,
                       const char *boundary,
                       const char *upload_dir,
                       char (*saved_names)[MAX_FILENAME+1],
                       long *saved_sizes,
                       int max_save, int *saved_count)
{
    *saved_count = 0;

    /* 构建边界标记 */
    char dash_boundary[BOUNDARY_MAX + 4];
    snprintf(dash_boundary, sizeof(dash_boundary), "--%s", boundary);
    int blen = strlen(dash_boundary);

    char close_boundary[BOUNDARY_MAX + 8];
    snprintf(close_boundary, sizeof(close_boundary), "--%s--", boundary);
    int clen = strlen(close_boundary);

    /* 查找第一个边界（可能在 body 开头，也可能前面有空白） */
    const char *p = body;
    const char *end = body + body_len;

    /* 跳过开头的空白/空行 */
    while (p < end && (*p == '\r' || *p == '\n' || *p == ' '))
        p++;

    /* 找到第一个 "--boundary" */
    if (p + blen > end || memcmp(p, dash_boundary, blen) != 0)
        return -1;  /* 没有边界标记 */

    /* 跳过第一个边界行 */
    p += blen;
    while (p < end && (*p == '\r' || *p == '\n'))
        p++;

    /* 遍历各部分 */
    while (p < end && *saved_count < max_save) {
        /* 查找下一个边界 "CRLF--boundary" 或 "CRLF--boundary--" */
        const char *next = NULL;
        int is_last = 0;

        const char *scan = p;
        while (scan < end) {
            /* 找 \r\n 或 \n */
            if (scan[0] == '\r' && scan + 1 < end && scan[1] == '\n') {
                scan += 2;
            } else if (scan[0] == '\n') {
                scan += 1;
            } else {
                scan++;
                continue;
            }

            /* 检查是否是 --boundary */
            if (end - scan >= blen && memcmp(scan, dash_boundary, blen) == 0) {
                next = scan;
                break;
            }
            /* 检查是否是 --boundary-- */
            if (end - scan >= clen && memcmp(scan, close_boundary, clen) == 0) {
                next = scan;
                is_last = 1;
                break;
            }
        }

        if (!next)
            break;  /* 没有更多边界 */

        /* 当前部分的原始数据范围（不含尾部 \r\n）：
         * 从 p 到 (next - 2) 或 (next - 1)，取决于边界前是 \r\n 还是 \n */
        const char *part_end = (next >= p + 2 && next[-2] == '\r' && next[-1] == '\n')
                               ? next - 2 : (next >= p + 1 && next[-1] == '\n')
                               ? next - 1 : next;
        long part_len = part_end - p;
        if (part_len < 0) part_len = 0;

        /* 在此部分中查找头部空行（\r\n\r\n 或 \n\n） */
        const char *hdr_end = NULL;
        for (const char *hp = p; hp + 1 < part_end; hp++) {
            if (hp[0] == '\r' && hp[1] == '\n') {
                if (hp + 3 < part_end && hp[2] == '\r' && hp[3] == '\n') {
                    hdr_end = hp + 4;
                    break;
                }
            }
            if (hp[0] == '\n' && hp + 1 < part_end && hp[1] == '\n') {
                hdr_end = hp + 2;
                break;
            }
        }

        if (!hdr_end) {
            p = next + blen;
            while (p < end && (*p == '\r' || *p == '\n'))
                p++;
            continue;
        }

        /* 提取 filename 从 Content-Disposition */
        char filename[MAX_FILENAME + 1];
        filename[0] = '\0';

        /* 在部分头部中搜索 Content-Disposition */
        long hdr_len = hdr_end - p;
        /* 用临时缓冲区复制头部（null 终止方便 strstr） */
        char hdr_buf[1024];
        long hdr_copy = hdr_len < (long)sizeof(hdr_buf) - 1 ? hdr_len : (long)sizeof(hdr_buf) - 1;
        memcpy(hdr_buf, p, hdr_copy);
        hdr_buf[hdr_copy] = '\0';

        char *cd = strstr(hdr_buf, "Content-Disposition");
        if (cd) {
            char *fn = strstr(cd, "filename=\"");
            if (fn) {
                fn += 10; /* skip "filename=\"" */
                int fi = 0;
                while (*fn && *fn != '"' && fi < MAX_FILENAME) {
                    filename[fi++] = *fn++;
                }
                filename[fi] = '\0';
            }
        }

        /* 提取数据体（hdr_end 到 part_end） */
        long data_len = part_end - hdr_end;
        if (data_len < 0) data_len = 0;

        /* 如果存在 filename，保存文件 */
        if (filename[0] != '\0') {
            char safe_name[MAX_FILENAME + 1];
            sanitize_filename(filename, safe_name, sizeof(safe_name));

            if (save_file_part(hdr_end, data_len, safe_name, upload_dir) == 0) {
                strncpy(saved_names[*saved_count], safe_name, MAX_FILENAME);
                saved_names[*saved_count][MAX_FILENAME] = '\0';
                saved_sizes[*saved_count] = data_len;
                (*saved_count)++;
            }
        }

        /* 跳到下一个边界（跳过当前边界的标记行） */
        p = next + blen;
        if (is_last)
            break;
        while (p < end && (*p == '\r' || *p == '\n'))
            p++;
    }

    return 0;
}

static void
handle_post_request(int fd, char *buf, int buf_len,
                    struct http_request *req, int body_offset,
                    struct webserv_thread_arg *targ)
{
    /* ---- 验证 Content-Length ---- */
    if (req->content_length < 0) {
        send_error(fd, 411, "Length Required",
                   "A Content-Length header is required for POST requests");
        return;
    }

    /* ---- 验证上传大小 ---- */
    if (req->content_length > MAX_UPLOAD_SIZE) {
        send_error(fd, 413, "Payload Too Large",
                   "Upload size exceeds the maximum allowed size (32 MB)");
        return;
    }

    /* ---- 验证 Content-Type ---- */
    if (req->content_type[0] == '\0' ||
        req->boundary[0] == '\0' ||
        !strstr(req->content_type, "multipart/form-data"))
    {
        send_error(fd, 415, "Unsupported Media Type",
                   "Only multipart/form-data uploads are supported");
        return;
    }

    /* ---- 解析上传目录路径 ---- */
    char decoded[PATH_MAX];
    url_decode(decoded, req->path, sizeof(decoded));
    strip_query_fragment(decoded);

    char full_path[PATH_MAX];
    if (strcmp(decoded, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "%s", targ->root_dir);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s",
                 targ->root_dir, decoded);
    }

    char upload_dir[PATH_MAX];
    tlibc_cal_absolute_path(full_path, NULL,
                            upload_dir, sizeof(upload_dir));

    if (!check_path_safe(upload_dir, targ->root_dir)) {
        send_error(fd, 403, "Forbidden",
                   "Directory traversal is not allowed");
        return;
    }

    /* ---- 检查目录是否存在 ---- */
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(upload_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
        send_error(fd, 404, "Not Found",
                   "The upload directory was not found");
        return;
    }

    /* ---- 构造显示路径（用于返回链接） ---- */
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

    /* ---- 读取请求体 ---- */
    char *body = NULL;
    if (recv_request_body(fd, buf, buf_len, body_offset,
                          req->content_length, &body) < 0 || !body)
    {
        send_error(fd, 400, "Bad Request",
                   "Failed to read the request body");
        return;
    }

    /* ---- 解析 multipart 并保存文件 ---- */
    char saved_names[16][MAX_FILENAME + 1];
    long saved_sizes[16];
    int saved_count = 0;

    parse_multipart_upload(body, req->content_length,
                           req->boundary, upload_dir,
                           saved_names, saved_sizes,
                           16, &saved_count);

    /* ---- 响应 ---- */
    send_upload_response(fd, saved_count, saved_names,
                         saved_sizes, display_path);

    munmap(body, req->content_length + 1);
}

/* ------------------------------------------------------------------ */
/*  Default Root Directory                                             */
/* ------------------------------------------------------------------ */

static int
get_default_root_dir(char *buf, size_t buf_size)
{
    char home[PATH_MAX];
    home[0] = '\0';

    /* 优先从环境变量 HOME 获取 */
    char *env_home = get_env_var(global_envp, "HOME");
    if (env_home && env_home[0] != '\0') {
        strncpy(home, env_home, sizeof(home) - 1);
        home[sizeof(home) - 1] = '\0';
    }

    /* 回退：解析 /etc/passwd */
    if (home[0] == '\0')
        tlibc_get_user_dir(home, sizeof(home));

    if (home[0] == '\0')
        return -1;

    snprintf(buf, buf_size, "%s/%s", home, TLIBC_WWW_DIR);

    /* 尝试创建目录（如果已存在则 EEXIST 不报错） */
    tlibc_recursive_mkdir(buf);

    /* 确认是有效目录 */
    if (tlibc_is_path_dir(buf) != 1) {
        printf("Warning: '%s' is not a valid directory\n", buf);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int port = WEBSERV_PORT;
    char root_dir[PATH_MAX];
    int root_dir_explicit = 0;

    /* 默认根目录：当前工作目录（临时，如用户未指定则后续覆盖） */
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
                if (getcwd(cwd, sizeof(cwd))) {
                    tlibc_cal_absolute_path(argv[2], cwd,
                                            root_dir, sizeof(root_dir));
                    root_dir_explicit = 1;
                }
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
                    root_dir_explicit = 1;
                } else {
                    printf("Error: '%s' is not a valid directory\n",
                           argv[1]);
                    return 1;
                }
            }
        }
    }

    /* 用户未指定根目录时，使用 ~/tlibc_www */
    if (!root_dir_explicit) {
        if (get_default_root_dir(root_dir, sizeof(root_dir)) != 0) {
            printf("Warning: could not create default www directory, "
                   "using current directory\n");
            /* 保持 cwd 作为 fallback */
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
