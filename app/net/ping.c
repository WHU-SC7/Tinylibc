/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * ping — 使用 ICMP 探测远程主机可达性与往返延迟
 *
 * 机制：构造 ICMP Echo Request，接收 Echo Reply，记录 RTT。
 *       原始套接字（SOCK_RAW, IPPROTO_ICMP）需要 CAP_NET_RAW。
 *
 * 系统调用：socket, sendto, recvfrom, setsockopt, clock_gettime
 *
 * 用法：
 *   ping <host>              # 默认发 4 个包
 *   ping <host> -c 10        # 指定发包数
 *   ping <host> -t 128       # 设置 TTL
 *   ping <host> -s 100       # 设置数据负载大小（不含 ICMP 头）
 *   Ctrl+C                   打印统计摘要并退出
 *
 * 索引：
 *   ping_main        入口：参数解析 → 循环发送/接收 → 统计输出
 *     send_ping      行 xx：构造并发送 ICMP Echo Request
 *     recv_reply     行 xx：接收并验证 ICMP Echo Reply
 *     print_stats    行 xx：输出丢包率与 RTT min/avg/max/mdev
 */

#include "core.h"
#include "tlibc_print.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "math.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"
#include "syscall.h"
#include "syscall_num.h"

/* ── 缺失的系统常量定义 ── */
#ifndef SOL_SOCKET
#define SOL_SOCKET 1
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 20
#endif

/* struct timeval（用于 setsockopt 超时） */
struct timeval_t {
    int64_t tv_sec;
    int64_t tv_usec;
};

/* ── ICMP 常量 ── */
#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP 1
#endif
#ifndef IP_TTL
#define IP_TTL 2
#endif
#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif

/* ── IP / ICMP 头部 ── */
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

struct icmphdr_t {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
};

struct icmp_payload {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/* ── 全局状态（用于 SIGINT 处理） ── */
static volatile int g_running = 1;
static int    g_sent   = 0;
static int    g_recvd  = 0;
static int64_t g_min_rtt = 999999999;
static int64_t g_max_rtt = 0;
static int64_t g_total_rtt = 0;
static int64_t g_total_rtt2 = 0;  /* 用于 mdev 计算 Σ(RTT²) */
static char    g_target_ip[16] = "";
static int     g_datalen = 56;    /* 默认 ICMP 数据负载大小 */

/* ── 校验和 ── */
static uint16_t in_cksum(const uint16_t *addr, int len)
{
    long sum = 0;
    while (len > 1) { sum += *addr++; len -= 2; }
    if (len == 1) sum += *(uint8_t *)addr;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

/* ── IP → 字符串 ── */
static void ip_to_str(unsigned int ip, char *buf, int buf_size)
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

/* ── 单调时间（纳秒） ── */
static int64_t time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000L + (int64_t)ts.tv_nsec;
}

/* ── SIGINT 处理：打印统计并退出 ── */
static void sigint_handler(int num)
{
    (void)num;
    g_running = 0;
}

static void print_stats(void)
{
    int loss = (g_sent > 0) ? ((g_sent - g_recvd) * 100 / g_sent) : 100;

    printf("\n--- %s ping statistics ---\n", g_target_ip);
    printf("  %d packets transmitted, %d received, %d%% packet loss\n",
           g_sent, g_recvd, loss);
    if (g_recvd > 0) {
        int64_t avg_ns = g_total_rtt / g_recvd;

        /* mdev = sqrt(Σ(RTT²)/N - (ΣRTT/N)²), 全部用 µs 防上溢 */
        int64_t avg_us  = avg_ns / 1000;
        int64_t avg_us2 = g_total_rtt2 / g_recvd;
        int64_t mdev_us = 0;
        if (avg_us2 > avg_us * avg_us) {
            int64_t variance = avg_us2 - avg_us * avg_us;
            mdev_us = isqrt(variance);
        }

        printf("  rtt min/avg/max/mdev = %lld.%03d/%lld.%03d/%lld.%03d/%lld.%03d ms\n",
               (long long)(g_min_rtt / 1000000),
               (int)((g_min_rtt % 1000000) / 1000),
               (long long)(avg_ns / 1000000),
               (int)((avg_ns % 1000000) / 1000),
               (long long)(g_max_rtt / 1000000),
               (int)((g_max_rtt % 1000000) / 1000),
               (long long)(mdev_us / 1000),
               (int)(mdev_us % 1000));
    }
}

/* ── ping 主循环 ── */
static void ping_main(const char *host, unsigned int ip, int count, int ttl)
{
    ip_to_str(ip, g_target_ip, sizeof(g_target_ip));

    printf("PING %s (%s) %d(%d) bytes of data.\n",
           host, g_target_ip, g_datalen, g_datalen + 8 + 20);

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf("ping: need CAP_NET_RAW (root or setcap cap_net_raw+ep)\n");
        return;
    }

    /* 设置 TTL */
    if (ttl > 0)
        setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

    /* 设置接收超时 1s */
    struct timeval_t tv;
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest;
    dest.sin_family      = AF_INET;
    dest.sin_port        = 0;
    dest.sin_addr.s_addr = ip;

    int payload_size = g_datalen;
    /* 确保至少能放时间戳 */
    if (payload_size < (int)sizeof(struct icmp_payload))
        payload_size = sizeof(struct icmp_payload);

    tlibc_sigaction(SIGINT, sigint_handler);

    for (int seq = 0; seq < count && g_running; seq++) {
        /* 构造 ICMP 包 */
        int pkt_size = sizeof(struct icmphdr_t) + payload_size;
        char send_buf[512];
        if (pkt_size > (int)sizeof(send_buf)) pkt_size = sizeof(send_buf);

        struct icmphdr_t icmp;
        icmp.type     = ICMP_ECHO_REQUEST;
        icmp.code     = 0;
        icmp.checksum = 0;
        icmp.id       = (uint16_t)(getpid() & 0xffff);
        icmp.sequence = (uint16_t)seq;

        memcpy(send_buf, &icmp, sizeof(icmp));

        /* 填充数据区：时间戳在前，其余模式填 0 */
        int64_t t_send = time_ns();
        struct icmp_payload pl;
        pl.tv_sec  = t_send / 1000000000L;
        pl.tv_nsec = t_send % 1000000000L;
        memcpy(send_buf + sizeof(icmp), &pl, sizeof(pl));
        if (payload_size > (int)sizeof(pl))
            __memset(send_buf + sizeof(icmp) + sizeof(pl), 0,
                     payload_size - sizeof(pl));

        /* 校验和 */
        ((struct icmphdr_t *)send_buf)->checksum =
            in_cksum((uint16_t *)send_buf, pkt_size);

        int n = syscall(SYS_sendto, sock, send_buf, pkt_size, 0,
                        &dest, sizeof(dest));
        if (n < 0) continue;
        g_sent++;

        /* 接收回复 */
        char recv_buf[1024];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        n = syscall(SYS_recvfrom, sock, recv_buf, sizeof(recv_buf), 0,
                     &from, &from_len);
        if (n < 0) {
            printf("  From %s: Request timed out\n", g_target_ip);
            continue;
        }

        struct iphdr_t *iph = (struct iphdr_t *)recv_buf;
        int ip_hdr_len = (iph->ihl & 0xf) * 4;
        struct icmphdr_t *icmp_r = (struct icmphdr_t *)(recv_buf + ip_hdr_len);

        /* 只接受本进程的 Echo Reply */
        if (icmp_r->type != ICMP_ECHO_REPLY ||
            icmp_r->id != (uint16_t)(getpid() & 0xffff)) {
            seq--;  /* 重试同一序号 */
            continue;
        }

        /* 检查回复包长度，确保时间戳完整 */
        int payload_off = ip_hdr_len + sizeof(struct icmphdr_t);
        if (n < payload_off + (int)sizeof(struct icmp_payload)) {
            seq--;
            continue;
        }

        /* RTT */
        struct icmp_payload *rp = (struct icmp_payload *)(recv_buf + payload_off);
        int64_t t_recv = time_ns();
        int64_t rtt_ns = t_recv - ((int64_t)rp->tv_sec * 1000000000L +
                                    (int64_t)rp->tv_nsec);
        if (rtt_ns < 0) rtt_ns = 0;

        g_recvd++;
        if (rtt_ns < g_min_rtt) g_min_rtt = rtt_ns;
        if (rtt_ns > g_max_rtt) g_max_rtt = rtt_ns;
        g_total_rtt  += rtt_ns;
        /* 用 µs² 存储平方和，避免 int64_t 上溢 */
        {
            int64_t rtt_us = rtt_ns / 1000;
            g_total_rtt2 += rtt_us * rtt_us;
        }

        int rtt_ms   = (int)(rtt_ns / 1000000);
        int rtt_frac = (int)((rtt_ns % 1000000) / 1000);
        if (rtt_ms == 0 && rtt_frac == 0) rtt_frac = 1;

        char from_ip[16];
        ip_to_str(from.sin_addr.s_addr, from_ip, sizeof(from_ip));
        printf("  %d bytes from %s: icmp_seq=%d ttl=%d time=%d.%03dms\n",
               n - ip_hdr_len, from_ip, seq, iph->ttl, rtt_ms, rtt_frac);

        /* 包间隔 1s */
        if (g_running && seq < count - 1)
            tlibc_msleep(1000);
    }

    close(sock);
    print_stats();
}

/* ── 入口 ── */
static void print_usage(void)
{
    printf("Usage: ping <host> [-c count] [-t ttl] [-s size]\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) { print_usage(); return 1; }

    const char *host = NULL;
    int count = 4;
    int ttl   = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'c': if (i + 1 < argc) count = atoi(argv[++i]); break;
            case 't': if (i + 1 < argc) ttl  = atoi(argv[++i]); break;
            case 's': if (i + 1 < argc) g_datalen = atoi(argv[++i]); break;
            }
        } else {
            host = argv[i];
        }
    }

    if (!host) { print_usage(); return 1; }

    unsigned int ip = tlibc_inet_addr(host);
    if (ip == 0xffffffff) {
        /* 尝试 DNS 解析 */
        uint32_t resolved;
        if (tlibc_dns_resolve(host, &resolved) < 0) {
            printf("ping: cannot resolve '%s'\n", host);
            return 1;
        }
        ip = resolved;
    }

    ping_main(host, ip, count, ttl);
    return 0;
}
