/* SPDX-License-Identifier: MIT
 *
 * netprobe — 网络延迟和拓扑探测
 *
 * 集成 ping + traceroute 功能，用于测量网络延迟和探测路由路径。
 *
 * 用法:
 *   netprobe ping <host>              # ping 模式（默认 4 包）
 *   netprobe ping <host> <count>      # 指定发包数
 *   netprobe traceroute <host>        # traceroute 模式
 *
 * 注意: 需要 root 权限（使用原始套接字发送/接收 ICMP）
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
/*  常量定义 — 标准 Linux 头文件中的值                                 */
/* ================================================================== */

#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP    1       /* ICMP 协议号 */
#endif
#ifndef IPPROTO_IP
#define IPPROTO_IP      0       /* IP 选项 */
#endif
#ifndef IP_TTL
#define IP_TTL          2       /* 设置 TTL 的 setsockopt 选项 */
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO     20      /* 接收超时 */
#endif

/* ICMP 类型码 */
/* 缺少的 sendto 系统调用号 */
#ifndef SYS_sendto
#define SYS_sendto  44
#endif

#define ICMP_ECHO_REPLY         0
#define ICMP_DEST_UNREACH       3
#define ICMP_ECHO_REQUEST       8
#define ICMP_TIME_EXCEEDED      11

/* traceroute 常量 */
#define TRACE_MAX_HOPS     30      /* 最大跳数 */
#define TRACE_TIMEOUT_S     3      /* 每跳超时(秒) */
#define TRACE_PROBES        3      /* 每跳探测次数 */
#define TRACE_START_PORT    33434  /* 起始目标端口 */

/* ================================================================== */
/*  类型定义 — IP / ICMP 包头                                        */
/* ================================================================== */

/* IPv4 头部（无选项的标准 20 字节） */
struct iphdr_t {
    uint8_t  ihl:4;
    uint8_t  version:4;
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
};

/* ICMP 头部 */
struct icmphdr_t {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
};

/* 时间戳负载（嵌入 ICMP 数据区，用于计算 RTT） */
struct icmp_payload {
    int64_t  tv_sec;
    int64_t  tv_nsec;
};

/* struct timeval（用于 setsockopt 超时） */
struct timeval_t {
    int64_t  tv_sec;
    int64_t  tv_usec;
};

/* ================================================================== */
/*  checksum — IP/ICMP 校验和                                         */
/* ================================================================== */

static uint16_t
in_cksum(const uint16_t *addr, int len)
{
    long sum = 0;
    while (len > 1) {
        sum += *addr++;
        len -= 2;
    }
    if (len == 1)
        sum += *(uint8_t *)addr;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

/* ================================================================== */
/*  辅助函数                                                          */
/* ================================================================== */

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

/* 获取单调时间（纳秒） */
static int64_t
time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000L + (int64_t)ts.tv_nsec;
}

/* ================================================================== */
/*  Ping 实现                                                         */
/* ================================================================== */

static void
ping_sweep(const char *host, unsigned int ip, int count)
{
    char ip_str[16];
    ip_to_str(ip, ip_str, sizeof(ip_str));

    printf("Pinging %s [%s] with 64 bytes of data:\n\n", host, ip_str);

    /* ---- 创建原始 ICMP 套接字 ---- */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf("Error: need root (CAP_NET_RAW) for ICMP socket: %d\n", sock);
        return;
    }

    /* 设置接收超时 */
    struct timeval_t tv;
    tv.tv_sec  = 1;      /* 1 秒超时 */
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest;
    dest.sin_family      = AF_INET;
    dest.sin_port        = 0;
    dest.sin_addr.s_addr = ip;

    int sent  = 0;
    int recvd = 0;
    int64_t min_rtt = 999999999;
    int64_t max_rtt = 0;
    int64_t total_rtt = 0;

    for (int seq = 0; seq < count; seq++) {
        struct icmphdr_t icmp;
        struct icmp_payload payload;

        icmp.type     = ICMP_ECHO_REQUEST;
        icmp.code     = 0;
        icmp.checksum = 0;
        icmp.id       = (uint16_t)(getpid() & 0xffff);
        icmp.sequence = (uint16_t)seq;

        /* 时间戳记入负载 */
        int64_t t_send = time_ns();
        payload.tv_sec  = t_send / 1000000000L;
        payload.tv_nsec = t_send % 1000000000L;

        /* 构造发送缓冲区：ICMP 头 + 时间戳负载 */
        char send_buf[sizeof(struct icmphdr_t) + sizeof(struct icmp_payload)];
        memcpy(send_buf, &icmp, sizeof(icmp));
        memcpy(send_buf + sizeof(icmp), &payload, sizeof(payload));

        /* 计算 ICMP 校验和 */
        ((struct icmphdr_t *)send_buf)->checksum = in_cksum((uint16_t *)send_buf, sizeof(send_buf));

        int n = syscall(SYS_sendto, sock, send_buf, sizeof(send_buf), 0,
                        &dest, sizeof(dest));
        if (n < 0) {
            printf("  Send failed (seq=%d)\n", seq);
            continue;
        }
        sent++;

        /* ---- 接收回复 ---- */
        char recv_buf[512];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        n = syscall(SYS_recvfrom, sock, recv_buf, sizeof(recv_buf), 0,
                     &from, &from_len);
        if (n < 0) {
            printf("  Reply from %s: %sRequest timed out%s\n",
                   ip_str, YELLOW_COLOR_PRINT, COLOR_RESET);
            continue;
        }

        /* 解析 IP 头 + ICMP 回复 */
        struct iphdr_t *iph = (struct iphdr_t *)recv_buf;
        int ip_hdr_len = (iph->ihl & 0xf) * 4;
        struct icmphdr_t *icmp_r = (struct icmphdr_t *)(recv_buf + ip_hdr_len);

        if (icmp_r->type != ICMP_ECHO_REPLY) {
            seq--;  /* 重试当前 seq */
            continue;
        }

        /* 提取时间戳计算 RTT */
        struct icmp_payload *rp = (struct icmp_payload *)(recv_buf + ip_hdr_len + sizeof(struct icmphdr_t));
        int64_t t_recv = time_ns();
        int64_t rtt_ns = t_recv - ((int64_t)rp->tv_sec * 1000000000L + (int64_t)rp->tv_nsec);
        /* 防止时钟跳跃导致负值 */
        if (rtt_ns < 0) rtt_ns = 0;

        int rtt_ms = (int)(rtt_ns / 1000000);
        int rtt_us = (int)((rtt_ns % 1000000) / 1000);
        if (rtt_ms == 0 && rtt_us == 0) rtt_us = 1;  /* 至少显示 0.xx ms */

        recvd++;
        if (rtt_ns < min_rtt) min_rtt = rtt_ns;
        if (rtt_ns > max_rtt) max_rtt = rtt_ns;
        total_rtt += rtt_ns;

        char from_ip[16];
        ip_to_str(from.sin_addr.s_addr, from_ip, sizeof(from_ip));
        printf("  %sReply from %s: bytes=%d time=%d.%dms TTL=%d%s\n",
               GREEN_COLOR_PRINT, from_ip,
               n - ip_hdr_len, rtt_ms, rtt_us, iph->ttl, COLOR_RESET);
    }

    close(sock);

    /* ---- 统计 ---- */
    int loss = (sent > 0) ? ((sent - recvd) * 100 / sent) : 100;
    int64_t avg_rtt = (recvd > 0) ? total_rtt / recvd : 0;

    printf("\n--- %s ping statistics ---\n", ip_str);
    printf("  %d packets transmitted, %d received, %d%c packet loss\n",
           sent, recvd, loss, 37);
    if (recvd > 0) {
        printf("  rtt min/avg/max = %d.%d/%d.%d/%d.%d ms\n",
               (int)(min_rtt / 1000000), (int)((min_rtt % 1000000) / 1000),
               (int)(avg_rtt / 1000000), (int)((avg_rtt % 1000000) / 1000),
               (int)(max_rtt / 1000000), (int)((max_rtt % 1000000) / 1000));
    }
}

/* ================================================================== */
/*  Traceroute 实现                                                   */
/* ================================================================== */

/* 发送一个 UDP 探测包并等待 ICMP 回复 */
static int
probe_hop(int udp_sock, int icmp_sock,
          unsigned int dest_ip, int dest_port, int ttl,
          unsigned int *resp_ip, int64_t *rtt_out)
{
    /* 设置 TTL */
    if (setsockopt(udp_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0)
        return -1;

    struct sockaddr_in dest;
    dest.sin_family      = AF_INET;
    dest.sin_port        = tlibc_htons((unsigned short)dest_port);
    dest.sin_addr.s_addr = dest_ip;

    char probe_buf[1] = { 0 };
    int64_t t0 = time_ns();

    if (syscall(SYS_sendto, udp_sock, probe_buf, 1, 0,
                &dest, sizeof(dest)) < 0)
        return -1;

    /* 等待 ICMP 回复 */
    char recv_buf[512];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    int n = syscall(SYS_recvfrom, icmp_sock, recv_buf, sizeof(recv_buf), 0,
                    &from, &from_len);
    if (n < 0)
        return -1;

    *rtt_out = time_ns() - t0;
    *resp_ip = from.sin_addr.s_addr;

    /* 解析 ICMP 类型 */
    struct iphdr_t *iph = (struct iphdr_t *)recv_buf;
    int ip_hdr_len = (iph->ihl & 0xf) * 4;
    struct icmphdr_t *icmp = (struct icmphdr_t *)(recv_buf + ip_hdr_len);

    if (icmp->type == ICMP_TIME_EXCEEDED)
        return 0;             /* 中间路由器 */
    if (icmp->type == ICMP_DEST_UNREACH)
        return 1;             /* 到达目的地 */

    return -1;
}

static void
traceroute(const char *host, unsigned int ip)
{
    char ip_str[16];
    ip_to_str(ip, ip_str, sizeof(ip_str));

    /* ---- 创建套接字 ---- */
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        printf("Error: cannot create UDP socket\n");
        return;
    }

    int icmp_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_sock < 0) {
        printf("Error: need root (CAP_NET_RAW) for traceroute: %d\n", icmp_sock);
        close(udp_sock);
        return;
    }

    /* 设置 ICMP 接收超时 */
    struct timeval_t tv;
    tv.tv_sec  = TRACE_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(icmp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("Tracing route to %s [%s]:\n", host, ip_str);
    printf("  max %d hops, %d probes per hop\n\n", TRACE_MAX_HOPS, TRACE_PROBES);

    int dest_reached = 0;

    for (int ttl = 1; ttl <= TRACE_MAX_HOPS && !dest_reached; ttl++) {
        /* 打印跳数 */
        printf("  %d  ", ttl);

        int hop_responses = 0;

        for (int p = 0; p < TRACE_PROBES; p++) {
            unsigned int resp_ip = 0;
            int64_t rtt = 0;
            int port = TRACE_START_PORT + ttl * TRACE_PROBES + p;

            int ret = probe_hop(udp_sock, icmp_sock, ip, port, ttl,
                                &resp_ip, &rtt);

            if (ret < 0) {
                /* 超时 */
                printf("  %s*%s", YELLOW_COLOR_PRINT, COLOR_RESET);
                continue;
            }

            char hop_ip[16];
            ip_to_str(resp_ip, hop_ip, sizeof(hop_ip));
            int rtt_ms = (int)(rtt / 1000000);
            int rtt_us = (int)((rtt % 1000000) / 1000);

            if (hop_responses == 0) {
                /* 本跳第一个成功的回复：显示 IP */
                const char *color = (ret == 1) ? BRIGHT_GREEN_COLOR_PRINT
                                               : CYAN_COLOR_PRINT;
                printf("  %s%s%s  %s%d.%dms%s",
                       color, hop_ip, COLOR_RESET,
                       color, rtt_ms, rtt_us, COLOR_RESET);
                hop_responses++;
            } else {
                printf("  %d.%dms", rtt_ms, rtt_us);
            }

            if (ret == 1) {
                dest_reached = 1;
                break;
            }
        }

        printf("\n");
    }

    if (dest_reached) {
        printf("\n  %sTrace complete.%s\n", GREEN_COLOR_PRINT, COLOR_RESET);
    } else {
        printf("\n  %sDestination not reached within %d hops.%s\n",
               YELLOW_COLOR_PRINT, TRACE_MAX_HOPS, COLOR_RESET);
    }

    close(udp_sock);
    close(icmp_sock);
}

/* ================================================================== */
/*  用法 / 主函数                                                     */
/* ================================================================== */

static void
print_usage(void)
{
    printf("Usage:\n");
    printf("  netprobe ping <host> [count=4]        ICMP ping\n");
    printf("  netprobe traceroute <host>              UDP traceroute\n\n");
    printf("Note: requires root (CAP_NET_RAW) for raw/ICMP sockets.\n");
}

int main(int argc, char *argv[])
{
    /* -h / --help 优先，不触发 argc 不足的错误 */
    if (argc >= 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage();
        return 0;
    }

    if (argc < 3) {
        print_usage();
        return 1;
    }

    const char *mode = argv[1];
    const char *host = argv[2];

    unsigned int target_ip;
    if (parse_ip(host, &target_ip) < 0)
        return 1;

    if (strcmp(mode, "ping") == 0) {
        int count = 4;
        if (argc >= 4) {
            /* 简单的字符串转整数 */
            int n = 0;
            const char *s = argv[3];
            while (*s) { n = n * 10 + (*s - '0'); s++; }
            if (n > 0 && n <= 100) count = n;
        }
        ping_sweep(host, target_ip, count);

    } else if (strcmp(mode, "traceroute") == 0) {
        traceroute(host, target_ip);

    } else {
        printf("Unknown mode: %s (use 'ping' or 'traceroute')\n", mode);
        return 1;
    }

    return 0;
}
