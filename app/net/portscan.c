/* SPDX-License-Identifier: MIT
 *
 * portscan — TCP Connect 端口扫描器
 *
 * 使用非阻塞 connect + poll 批量扫描 TCP 端口。
 * 不需要 root 权限（普通 TCP connect 即可）。
 *
 * 用法:
 *   portscan <host>
 *   portscan <host> <port>
 *   portscan <host> <start> <end>
 *
 * 示例:
 *   portscan 127.0.0.1          # 扫描 1-1024 端口
 *   portscan 192.168.1.1 22    # 检查单端口 SSH
 *   portscan 10.0.0.1 1 1000   # 扫描 1-1000
 *
 * 编译: make all 或 tmake 自动构建
 */

#include "core.h"
#include "tlibc_print.h"
#include "errno.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"
#include "syscall.h"
#include "syscall_num.h"

/* ================================================================== */
/*  常量                                                              */
/* ================================================================== */

#define MAX_BATCH   96       /* 单批最大并发连接数 — 避免 fd 耗尽     */
#define TIMEOUT_MS  2500     /* 连接超时 (ms)                         */

/* ================================================================== */
/*  端口颜色 — 按类型分色显示                                        */
/* ================================================================== */

/* 根据端口号返回对应 ANSI 颜色 */
static const char *
port_color(unsigned short port)
{
    if (port < 1024)
        return CYAN_COLOR_PRINT;      /* 知名端口（系统服务） */
    if (port < 49152)
        return GREEN_COLOR_PRINT;     /* 注册端口（用户应用） */
    return YELLOW_COLOR_PRINT;        /* 动态/私有端口 */
}

/* ================================================================== */
/*  服务名称猜测                                                      */
/* ================================================================== */

static const struct {
    unsigned short port;
    const char    *name;
} g_services[] = {
    { 21,    "FTP" },
    { 22,    "SSH" },
    { 23,    "Telnet" },
    { 25,    "SMTP" },
    { 53,    "DNS" },
    { 80,    "HTTP" },
    { 110,   "POP3" },
    { 111,   "RPC" },
    { 135,   "EPMAP" },
    { 137,   "NetBIOS-NS" },
    { 139,   "NetBIOS-SSN" },
    { 143,   "IMAP" },
    { 161,   "SNMP" },
    { 389,   "LDAP" },
    { 443,   "HTTPS" },
    { 445,   "SMB" },
    { 465,   "SMTPS" },
    { 514,   "Syslog" },
    { 587,   "SMTP-Sub" },
    { 631,   "IPP" },
    { 636,   "LDAPS" },
    { 993,   "IMAPS" },
    { 995,   "POP3S" },
    { 1080,  "SOCKS" },
    { 1194,  "OpenVPN" },
    { 1433,  "MSSQL" },
    { 1521,  "Oracle-DB" },
    { 2049,  "NFS" },
    { 2082,  "cPanel" },
    { 2083,  "cPanel-SSL" },
    { 2222,  "SSH-Alt" },
    { 2375,  "Docker" },
    { 2376,  "Docker-SSL" },
    { 3000,  "Dev-HTTP" },
    { 3128,  "Squid" },
    { 3306,  "MySQL" },
    { 3389,  "RDP" },
    { 3690,  "SVN" },
    { 4000,  "Dev-Alt" },
    { 4369,  "EPMD" },
    { 5432,  "PostgreSQL" },
    { 5555,  "ADB" },
    { 5672,  "RabbitMQ" },
    { 5900,  "VNC" },
    { 5901,  "VNC-1" },
    { 5984,  "CouchDB" },
    { 6379,  "Redis" },
    { 6443,  "K8s-API" },
    { 7077,  "Mesos" },
    { 8080,  "HTTP-Alt" },
    { 8443,  "HTTPS-Alt" },
    { 9000,  "PHP-FPM" },
    { 9001,  "Tor" },
    { 9042,  "Cassandra" },
    { 9090,  "Prometheus" },
    { 9092,  "Kafka" },
    { 9100,  "Node-Exp" },
    { 9200,  "Elasticsearch" },
    { 9418,  "Git" },
    { 11211, "Memcached" },
    { 15672, "RabbitMQ-A" },
    { 27017, "MongoDB" },
    { 32400, "Plex" },
};

static const char *
guess_service(unsigned short port)
{
    for (size_t i = 0; i < sizeof(g_services)/sizeof(g_services[0]); i++) {
        if (g_services[i].port == port)
            return g_services[i].name;
    }
    return NULL;
}

/* ================================================================== */
/*  辅助函数                                                          */
/* ================================================================== */

/* 解析点分十进制 IP */
static int
parse_ip(const char *host, unsigned int *ip_out)
{
    unsigned int addr = tlibc_inet_addr(host);
    if (addr == 0xffffffff) {
        printf("Error: cannot resolve '%s' — numeric IP only\n", host);
        return -1;
    }
    *ip_out = addr;
    return 0;
}

/* 将 IP（网络字节序）写入点分十进制字符串 */
static void
ip_to_str(unsigned int ip, char *buf, int buf_size)
{
    unsigned char *b = (unsigned char *)&ip;
    if (buf_size < 16) { buf[0] = '\0'; return; }
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        if (i > 0) buf[pos++] = '.';
        unsigned int v = b[i];
        if (v >= 100) buf[pos++] = '0' + (v / 100);
        if (v >= 10)  buf[pos++] = '0' + ((v % 100) / 10);
        buf[pos++] = '0' + (v % 10);
    }
    buf[pos] = '\0';
}

/* 将字符串转换为整数，返回 -1 表示无效 */
static int
parse_int(const char *s, int min, int max)
{
    int n = 0;
    if (!s || !*s) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        n = n * 10 + (*s - '0');
        if (n > max) return -1;
        s++;
    }
    if (n < min) return -1;
    return n;
}

/* ================================================================== */
/*  批量端口扫描核心                                                  */
/* ================================================================== */

/*
 * 对 [start_port, start_port+count) 范围内的端口发起非阻塞 TCP connect，
 * 用 poll 等待结果，将开放端口写入 results，result_count 递增。
 */
static void
scan_batch(unsigned int ip, int start_port, int count,
           int *results, int *result_count)
{
    struct pollfd pfds[MAX_BATCH];
    int socks[MAX_BATCH];
    int ports[MAX_BATCH];
    int n = 0;

    if (count > MAX_BATCH)
        count = MAX_BATCH;

    /* 第 1 步：创建套接字，发起非阻塞 connect */
    for (int i = 0; i < count; i++) {
        int port = start_port + i;

        int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sock < 0)
            continue;

        struct sockaddr_in addr;
        addr.sin_family      = AF_INET;
        addr.sin_port        = tlibc_htons((unsigned short)port);
        addr.sin_addr.s_addr = ip;

        int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == 0) {
            /* 立即成功（通常发生在本机回环地址） */
            results[*result_count] = port;
            (*result_count)++;
            close(sock);
            continue;
        }

        if (ret != -EINPROGRESS) {
            /* 其他错误（拒绝、不可达等），不加入 poll */
            close(sock);
            continue;
        }

        /* 正常情况返回 -EINPROGRESS，加入 poll 等待结果 */
        ports[n] = port;
        socks[n] = sock;
        pfds[n].fd      = sock;
        pfds[n].events  = POLLOUT;
        pfds[n].revents = 0;
        n++;
    }

    if (n == 0)
        return;

    /* 第 2 步：等待连接结果 */
    int ready = poll(pfds, (unsigned long)n, TIMEOUT_MS);

    if (ready > 0) {
        for (int i = 0; i < n; i++) {
            if (pfds[i].revents & (POLLERR | POLLHUP))
                continue;                     /* 连接被拒绝或超时 */
            if (pfds[i].revents & POLLOUT) {
                results[*result_count] = ports[i];
                (*result_count)++;
            }
        }
    }

    /* 第 3 步：关闭所有套接字 */
    for (int i = 0; i < n; i++)
        close(socks[i]);
}

/* ================================================================== */
/*  启动信息与用法                                                    */
/* ================================================================== */

static void
print_usage(void)
{
    printf("Usage: portscan <host> [start_port [end_port]]\n");
    printf("\n");
    printf("TCP connect port scanner — no root required.\n");
    printf("Scans in batches of %d with %dms timeout.\n\n", MAX_BATCH, TIMEOUT_MS);
    printf("Examples:\n");
    printf("  portscan 127.0.0.1          # scan ports 1-1024\n");
    printf("  portscan 192.168.1.1 22     # single port (SSH)\n");
    printf("  portscan 10.0.0.1 1 65535   # full scan\n");
}

/* ================================================================== */
/*  主函数                                                            */
/* ================================================================== */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    /* ---- 解析目标 IP ---- */
    unsigned int target_ip;
    if (parse_ip(argv[1], &target_ip) < 0)
        return 1;

    /* ---- 解析端口范围 ---- */
    int start_port = 1;
    int end_port   = 1024;

    if (argc >= 3) {
        start_port = parse_int(argv[2], 1, 65535);
        if (start_port < 0) {
            printf("Invalid port: %s (must be 1-65535)\n", argv[2]);
            return 1;
        }
        end_port = start_port;
    }

    if (argc >= 4) {
        end_port = parse_int(argv[3], 1, 65535);
        if (end_port < 0 || end_port < start_port) {
            printf("Invalid end port (must be 1-65535 and >= start)\n");
            return 1;
        }
    }

    /* ---- 显示扫描概要 ---- */
    char ip_str[16];
    ip_to_str(target_ip, ip_str, sizeof(ip_str));

    int total_ports = end_port - start_port + 1;
    printf("Port Scanner — %s  ports %d-%d (%d total)\n\n",
           ip_str, start_port, end_port, total_ports);

    /* ---- 批量扫描 ---- */
    int open_total = 0;
    int results[MAX_BATCH];
    int scanned = 0;

    for (int port = start_port; port <= end_port; ) {
        int remaining = end_port - port + 1;
        int batch     = remaining > MAX_BATCH ? MAX_BATCH : remaining;
        int found     = 0;

        scan_batch(target_ip, port, batch, results, &found);

        /* 显示本批开放的端口 */
        for (int i = 0; i < found; i++) {
            int p = results[i];
            const char *color = port_color((unsigned short)p);
            const char *svc = guess_service((unsigned short)p);
            printf("  %s%d/tcp   open" COLOR_RESET, color, p);
            if (svc)
                printf("   [%s]", svc);
            printf("\n");
        }
        open_total += found;
        scanned += batch;

        /* 进度条（回车覆盖） */
        {
            int pct = (scanned * 100) / total_ports;
            /* %%不被printf支持，用%c(37)代替 */
            printf("\r  Progress: %d/%d (%d%c)  ",
                   scanned, total_ports, pct, 37);
        }

        port += batch;
    }

    /* 如果没有任何端口开放，在进度行后追加提示 */
    if (open_total == 0) {
        printf("\n  (no open ports found)\n");
    }

    printf("\n\n--- %d ports scanned, %d open ---\n",
           total_ports, open_total);
    return 0;
}
