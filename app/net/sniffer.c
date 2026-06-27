/* SPDX-License-Identifier: MIT
 *
 * sniffer — 轻量网络数据包嗅探器
 *
 * 使用 AF_PACKET + SOCK_RAW 捕获原始网络帧，解析并清晰显示
 * Ethernet、IPv4、ARP、TCP、UDP、ICMP 协议头部及载荷。
 *
 * 用法:
 *   sniffer                     # 捕获所有协议
 *   sniffer tcp                 # 只显示 TCP
 *   sniffer udp                 # 只显示 UDP
 *   sniffer icmp                # 只显示 ICMP
 *   sniffer arp                 # 只显示 ARP
 *   sniffer ip                  # 只显示 IPv4
 *   sniffer tcp 80              # 只显示 TCP，且源或目标端口为 80
 *   sniffer udp 53              # 只显示 UDP，且源或目标端口为 53
 *
 * 注意: 需要 root 权限 (CAP_NET_RAW) 运行。
 *       在 WSL2 下由于 Hyper-V 虚拟交换机限制，可能只能捕获
 *       loopback 流量或少量外部流量。
 *
 * 编译: 在项目根目录下执行 make all 或 tmake 即可自动构建。
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
/*  Linux 网络常量 — 项目头文件未提供，在此定义                        */
/* ================================================================== */

#define AF_PACKET       17
#define ETH_P_ALL       0x0003   /* 捕获所有 Ethernet 协议  */
#define ETH_P_IP        0x0800   /* IPv4                   */
#define ETH_P_ARP       0x0806   /* ARP                    */
#define ETH_P_IPV6      0x86DD   /* IPv6                   */

#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

#define ETH_ALEN        6        /* MAC 地址长度             */
#define ETH_HLEN        14       /* Ethernet 头部长度         */
#define MAX_PACKET_SIZE 65536    /* 最大捕获帧大小             */

/* ================================================================== */
/*  协议头部结构                                                       */
/* ================================================================== */

/* sockaddr_ll — AF_PACKET 地址结构 */
struct sockaddr_ll {
    unsigned short sll_family;   /* 始终 AF_PACKET           */
    unsigned short sll_protocol; /* 物理层协议 (网络字节序)  */
    int            sll_ifindex;  /* 接口索引                 */
    unsigned short sll_hatype;   /* ARP 硬件类型              */
    unsigned char  sll_pkttype;  /* 包类型                   */
    unsigned char  sll_halen;    /* MAC 地址长度              */
    unsigned char  sll_addr[8];  /* MAC 地址                  */
};

/* Ethernet 头部 — 14 字节 */
struct eth_hdr {
    unsigned char  dst[ETH_ALEN];
    unsigned char  src[ETH_ALEN];
    unsigned short type;         /* 网络字节序                */
} __attribute__((packed));

/* IPv4 头部 — 20 字节（不含选项） */
struct ip_hdr {
    unsigned char  ver_ihl;      /* 版本(4) + 头部长度(4)   */
    unsigned char  dscp_ecn;     /* DSCP + ECN               */
    unsigned short total_len;    /* 总长度 (网络字节序)       */
    unsigned short id;           /* 标识                     */
    unsigned short frag_off;     /* 标志 + 片偏移             */
    unsigned char  ttl;          /* 生存时间                 */
    unsigned char  proto;        /* 上层协议号                */
    unsigned short csum;         /* 头部校验和                */
    unsigned int   src;          /* 源 IP (网络字节序)        */
    unsigned int   dst;          /* 目标 IP (网络字节序)      */
} __attribute__((packed));

/* TCP 头部 — 20 字节（不含选项） */
struct tcp_hdr {
    unsigned short src_port;     /* 源端口 (网络字节序)      */
    unsigned short dst_port;     /* 目标端口 (网络字节序)    */
    unsigned int   seq;          /* 序列号                   */
    unsigned int   ack_seq;      /* 确认号                   */
    unsigned short offset_res;   /* 数据偏移(4) + 保留(6) + 标志(6) */
    unsigned short window;       /* 窗口大小 (网络字节序)    */
    unsigned short csum;         /* 校验和                   */
    unsigned short urg_ptr;      /* 紧急指针                 */
} __attribute__((packed));

/* UDP 头部 — 8 字节 */
struct udp_hdr {
    unsigned short src_port;     /* 源端口 (网络字节序)      */
    unsigned short dst_port;     /* 目标端口 (网络字节序)    */
    unsigned short len;          /* 长度 (网络字节序)        */
    unsigned short csum;         /* 校验和                   */
} __attribute__((packed));

/* ICMP 头部 — 4 字节（不含可变部分） */
struct icmp_hdr {
    unsigned char  type;
    unsigned char  code;
    unsigned short csum;
} __attribute__((packed));

/* ARP 头部 — 28 字节 */
struct arp_hdr {
    unsigned short htype;        /* 硬件类型 (网络字节序)    */
    unsigned short ptype;        /* 协议类型 (网络字节序)    */
    unsigned char  hlen;         /* 硬件地址长度             */
    unsigned char  plen;         /* 协议地址长度             */
    unsigned short oper;         /* 操作码 (网络字节序)      */
    unsigned char  sha[ETH_ALEN];/* 发送方 MAC               */
    unsigned int   spa;          /* 发送方 IP (网络字节序)   */
    unsigned char  tha[ETH_ALEN];/* 目标 MAC                 */
    unsigned int   tpa;          /* 目标 IP (网络字节序)     */
} __attribute__((packed));

/* ================================================================== */
/*  全局过滤状态                                                       */
/* ================================================================== */

static const char *g_filter_proto; /* "tcp"/"udp"/"icmp"/"arp"/"ip"/NULL */
static int          g_filter_port; /* 端口过滤，-1 表示不过滤    */

/* ================================================================== */
/*  辅助函数 — 手动格式化（项目 printf/snprintf 不支持宽度/补零）    */
/* ================================================================== */

/* 通过 SYS_recvfrom 直接调用内核（与 recv() 共享同一系统调用号，
 * 但额外提供 src_addr 和 addrlen 两个参数用于 AF_PACKET 返回接口信息）*/
static int
__recvfrom(int sockfd, void *buf, unsigned long len, int flags,
           struct sockaddr_ll *src_addr, unsigned int *addrlen)
{
    return syscall(SYS_recvfrom, sockfd, buf, len, flags,
                   src_addr, addrlen);
}

/* 将 MAC 地址格式化为 "xx:xx:xx:xx:xx:xx"，写入调用者提供的缓冲区 */
static void
mac_to_str_buf(const unsigned char *mac, char *buf, int buf_size)
{
    static const char hex[] = "0123456789abcdef";
    int pos = 0;
    if (buf_size < 18) { buf[0] = '\0'; return; }
    for (int i = 0; i < 6; i++) {
        if (i > 0) buf[pos++] = ':';
        buf[pos++] = hex[(mac[i] >> 4) & 0x0f];
        buf[pos++] = hex[mac[i] & 0x0f];
    }
    buf[pos] = '\0';
}

/* 将 uint32 IP（网络字节序）格式化为点分十进制，写入调用者提供的缓冲区 */
static void
ip_to_str_buf(unsigned int ip, char *buf, int buf_size)
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

/* 写一个十六进制字节到 buf，返回写的位置 */
static int
hex_byte(char *buf, unsigned char c)
{
    static const char hex[] = "0123456789abcdef";
    buf[0] = hex[(c >> 4) & 0x0f];
    buf[1] = hex[c & 0x0f];
    return 2;
}

/* 写一个十六进制 uint（补零到 width 位）到 buf，返回写的位置 */
static int
hex_uint(char *buf, unsigned int val, int width)
{
    static const char hex[] = "0123456789abcdef";
    int pos = 0;
    for (int i = width - 1; i >= 0; i--) {
        buf[pos++] = hex[(val >> (i * 4)) & 0x0f];
    }
    return pos;
}

/* 写一个十进制 uint 到 buf，返回写的位置 */
static int
dec_uint(char *buf, unsigned int val)
{
    char tmp[16];
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        return 1;
    }
    while (val) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int pos = 0;
    while (i > 0)
        buf[pos++] = tmp[--i];
    return pos;
}

/* 获取含毫秒的时间戳字符串，写入调用者提供的缓冲区 */
static void
timestamp_str_buf(char *buf, int buf_size)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    if (buf_size < 24) { buf[0] = '\0'; return; }

    long total_sec = (long)ts.tv_sec;
    long h = (total_sec % 86400) / 3600;
    long m = (total_sec % 3600) / 60;
    long s = total_sec % 60;
    long ms = ts.tv_nsec / 1000000;

    char tmp[32];
    int pos = 0;
    /* 小时 */
    if (h < 10) tmp[pos++] = '0';
    pos += dec_uint(tmp + pos, (unsigned int)h);
    tmp[pos++] = ':';
    /* 分钟 */
    if (m < 10) tmp[pos++] = '0';
    pos += dec_uint(tmp + pos, (unsigned int)m);
    tmp[pos++] = ':';
    /* 秒 */
    if (s < 10) tmp[pos++] = '0';
    pos += dec_uint(tmp + pos, (unsigned int)s);
    tmp[pos++] = '.';
    /* 毫秒 */
    if (ms < 100) tmp[pos++] = '0';
    if (ms < 10)  tmp[pos++] = '0';
    pos += dec_uint(tmp + pos, (unsigned int)ms);
    tmp[pos] = '\0';

    /* 复制到输出 buffer */
    for (int i = 0; i <= pos; i++)
        buf[i] = tmp[i];
}

/* 十六进制 + ASCII 转储 — 手动格式化（printf 不支持 %02x/%04x） */
static void
hex_dump(const unsigned char *data, int len, int indent)
{
    /* 写缩进 */
    for (int i = 0; i < indent; i++)
        printf(" ");

    int offset = 0;
    while (offset < len) {
        int remaining = len - offset;
        int chunk = remaining > 16 ? 16 : remaining;

        /* 在当前行首 indented，后续行缩进更多 */
        if (offset > 0) {
            for (int i = 0; i < indent; i++)
                printf(" ");
        }

        /* 偏移 */
        {
            char hb[8];
            hex_uint(hb, (unsigned int)offset, 4);
            hb[4] = '\0';
            printf("%s  ", hb);
        }

        /* 十六进制 */
        for (int i = 0; i < 16; i++) {
            if (i < chunk) {
                char hb[4];
                hex_byte(hb, data[offset + i]);
                hb[2] = '\0';
                printf("%s ", hb);
            } else {
                printf("   ");
            }
            if (i == 7) printf(" ");
        }

        /* ASCII */
        printf(" ");
        for (int i = 0; i < chunk; i++) {
            unsigned char c = data[offset + i];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
        offset += chunk;
    }
}

/* TCP 标志位转字符串 */
static const char *
tcp_flags_str(unsigned short flags)
{
    static char buf[32];
    unsigned short f = flags & 0x3f;
    int pos = 0;

#define FLAG_APPEND(s) do { \
    const char *_p = (s); \
    if (pos > 0) buf[pos++] = ','; \
    while (*_p) buf[pos++] = *_p++; \
} while(0)

    if (f & 0x20) FLAG_APPEND("FIN");
    if (f & 0x10) FLAG_APPEND("SYN");
    if (f & 0x08) FLAG_APPEND("RST");
    if (f & 0x04) FLAG_APPEND("PSH");
    if (f & 0x02) FLAG_APPEND("ACK");
    if (f & 0x01) FLAG_APPEND("URG");
    if (pos == 0) buf[pos++] = '-';
    buf[pos] = '\0';
    return buf;

#undef FLAG_APPEND
}

/* ================================================================== */
/*  应用层协议猜测 — 按端口号显示协议名称                              */
/* ================================================================== */

static const char *
guess_app_proto(unsigned short port)
{
    switch (port) {
        case 80:   return "HTTP";
        case 443:  return "HTTPS";
        case 22:   return "SSH";
        case 53:   return "DNS";
        case 21:   return "FTP";
        case 25:   return "SMTP";
        case 110:  return "POP3";
        case 143:  return "IMAP";
        case 3306: return "MySQL";
        case 6379: return "Redis";
        case 8080: return "HTTP-ALT";
        default:   return NULL;
    }
}

/* ================================================================== */
/*  过滤逻辑                                                           */
/* ================================================================== */

/* 检查给定协议是否应被显示。
 * - 未设置过滤器时，显示所有协议。
 * - 过滤器为 "ip" 时，TCP/UDP/ICMP 均匹配（显示所有 IP 包）。
 * - 其他情况精确匹配。 */
static int
should_display_packet(const char *proto_name)
{
    if (!g_filter_proto)
        return 1;

    if (strcmp(g_filter_proto, "ip") == 0) {
        return (strcmp(proto_name, "tcp") == 0 ||
                strcmp(proto_name, "udp") == 0 ||
                strcmp(proto_name, "icmp") == 0);
    }

    return (strcmp(g_filter_proto, proto_name) == 0);
}

/* ================================================================== */
/*  包解析与显示                                                       */
/* ================================================================== */

/* 显示 Ethernet 头部 */
static void
print_eth(const struct eth_hdr *eth)
{
    unsigned short type = tlibc_ntohs(eth->type);
    const char *type_str;
    switch (type) {
        case 0x0800: type_str = "IPv4"; break;
        case 0x0806: type_str = "ARP";  break;
        case 0x86DD: type_str = "IPv6"; break;
        default:     type_str = NULL;   break;
    }

    char src_mac[18], dst_mac[18];
    mac_to_str_buf(eth->src, src_mac, sizeof(src_mac));
    mac_to_str_buf(eth->dst, dst_mac, sizeof(dst_mac));

    if (type_str)
        printf("  ETH: %s -> %s | Type: %s\n",
               src_mac, dst_mac, type_str);
    else
        printf("  ETH: %s -> %s | Type: 0x%x\n",
               src_mac, dst_mac, type);
}

/* 显示 IPv4 头部 */
static void
print_ip(const struct ip_hdr *ip, int *ip_hdr_len)
{
    *ip_hdr_len = (ip->ver_ihl & 0x0f) * 4;

    int total_len = tlibc_ntohs(ip->total_len);
    const char *proto_str;
    switch (ip->proto) {
        case IPPROTO_TCP:  proto_str = "TCP";  break;
        case IPPROTO_UDP:  proto_str = "UDP";  break;
        case IPPROTO_ICMP: proto_str = "ICMP"; break;
        default:           proto_str = NULL;   break;
    }

    char src_ip[16], dst_ip[16];
    ip_to_str_buf(ip->src, src_ip, sizeof(src_ip));
    ip_to_str_buf(ip->dst, dst_ip, sizeof(dst_ip));

    if (proto_str)
        printf("  IP:  %s -> %s | TTL: %d | Proto: %s (%d) | Len: %d\n",
               src_ip, dst_ip,
               ip->ttl, proto_str, ip->proto, total_len);
    else
        printf("  IP:  %s -> %s | TTL: %d | Proto: %d | Len: %d\n",
               src_ip, dst_ip,
               ip->ttl, ip->proto, total_len);
}

/* 检查端口过滤 */
static int
port_matches(unsigned short sport, unsigned short dport)
{
    if (g_filter_port < 0)
        return 1;
    unsigned short p = (unsigned short)g_filter_port;
    return (sport == p || dport == p);
}

/* TCP 包解析与显示 */
static void
handle_tcp(const unsigned char *packet, int ip_hdr_len,
           int remaining, const struct ip_hdr *ip)
{
    if (remaining < (int)sizeof(struct tcp_hdr))
        return;

    const struct tcp_hdr *tcp = (const struct tcp_hdr *)(packet + ETH_HLEN + ip_hdr_len);
    unsigned short sport = tlibc_ntohs(tcp->src_port);
    unsigned short dport = tlibc_ntohs(tcp->dst_port);

    if (!port_matches(sport, dport) || !should_display_packet("tcp"))
        return;

    int tcp_hdr_len = ((tcp->offset_res >> 12) & 0x0f) * 4;
    if (tcp_hdr_len < 20) tcp_hdr_len = 20;

    int payload_len = remaining - tcp_hdr_len;
    if (payload_len < 0) payload_len = 0;

    const char *app_src = guess_app_proto(sport);
    const char *app_dst = guess_app_proto(dport);

    char ts[24];
    timestamp_str_buf(ts, sizeof(ts));
    printf("[%s] TCP: %u -> %u | Flags: [%s] | Seq: %u | Ack: %u | Win: %u | Len: %d",
           ts, sport, dport,
           tcp_flags_str(tcp->offset_res),
           tcp->seq, tcp->ack_seq,
           tlibc_ntohs(tcp->window), payload_len);
    if (app_src || app_dst)
        printf(" (%s%s%s)", app_src ? app_src : "",
               (app_src && app_dst) ? "<->" : "",
               app_dst ? app_dst : "");
    printf("\n");

    char src_ip[16], dst_ip[16];
    ip_to_str_buf(ip->src, src_ip, sizeof(src_ip));
    ip_to_str_buf(ip->dst, dst_ip, sizeof(dst_ip));
    printf("       %s:%u -> %s:%u\n",
           src_ip, sport,
           dst_ip, dport);

    if (payload_len > 0) {
        int show = payload_len > 128 ? 128 : payload_len;
        printf("       Payload %d bytes:\n", payload_len);
        hex_dump(packet + ETH_HLEN + ip_hdr_len + tcp_hdr_len, show, 7);
        if (payload_len > 128)
            printf("       ... (truncated, %d more bytes)\n", payload_len - 128);
    }
}

/* UDP 包解析与显示 */
static void
handle_udp(const unsigned char *packet, int ip_hdr_len,
           int remaining, const struct ip_hdr *ip)
{
    if (remaining < (int)sizeof(struct udp_hdr))
        return;

    const struct udp_hdr *udp = (const struct udp_hdr *)(packet + ETH_HLEN + ip_hdr_len);
    unsigned short sport = tlibc_ntohs(udp->src_port);
    unsigned short dport = tlibc_ntohs(udp->dst_port);

    if (!port_matches(sport, dport) || !should_display_packet("udp"))
        return;

    int payload_len = tlibc_ntohs(udp->len) - sizeof(struct udp_hdr);
    if (payload_len < 0) payload_len = 0;
    int remain_after_udp = remaining - sizeof(struct udp_hdr);
    if (payload_len > remain_after_udp)
        payload_len = remain_after_udp;

    const char *app_src = guess_app_proto(sport);
    const char *app_dst = guess_app_proto(dport);

    char ts[24];
    timestamp_str_buf(ts, sizeof(ts));
    printf("[%s] UDP: %u -> %u | Len: %d",
           ts, sport, dport,
           tlibc_ntohs(udp->len));
    if (app_src || app_dst)
        printf(" (%s%s%s)", app_src ? app_src : "",
               (app_src && app_dst) ? "<->" : "",
               app_dst ? app_dst : "");
    printf("\n");

    char src_ip[16], dst_ip[16];
    ip_to_str_buf(ip->src, src_ip, sizeof(src_ip));
    ip_to_str_buf(ip->dst, dst_ip, sizeof(dst_ip));
    printf("       %s:%u -> %s:%u\n",
           src_ip, sport,
           dst_ip, dport);

    if (payload_len > 0) {
        int show = payload_len > 128 ? 128 : payload_len;
        printf("       Payload %d bytes:\n", payload_len);
        hex_dump(packet + ETH_HLEN + ip_hdr_len + sizeof(struct udp_hdr),
                 show, 7);
        if (payload_len > 128)
            printf("       ... (truncated, %d more bytes)\n", payload_len - 128);
    }
}

/* ICMP 包解析与显示 */
static void
handle_icmp(const unsigned char *packet, int ip_hdr_len,
            int remaining)
{
    if (remaining < (int)sizeof(struct icmp_hdr))
        return;
    if (!should_display_packet("icmp"))
        return;

    const struct icmp_hdr *icmp = (const struct icmp_hdr *)(packet + ETH_HLEN + ip_hdr_len);

    const char *type_str;
    switch (icmp->type) {
        case 0:  type_str = "Echo Reply";      break;
        case 3:  type_str = "Dest Unreachable"; break;
        case 8:  type_str = "Echo Request";    break;
        case 11: type_str = "Time Exceeded";   break;
        default: type_str = "Other";           break;
    }

    int payload_len = remaining - sizeof(struct icmp_hdr);
    if (payload_len < 0) payload_len = 0;

    char ts[24];
    timestamp_str_buf(ts, sizeof(ts));
    printf("[%s] ICMP: %s (type=%d, code=%d)\n",
           ts, type_str, icmp->type, icmp->code);

    if (payload_len > 0) {
        int show = payload_len > 64 ? 64 : payload_len;
        printf("       Payload %d bytes:\n", payload_len);
        hex_dump(packet + ETH_HLEN + ip_hdr_len + sizeof(struct icmp_hdr),
                 show, 7);
        if (payload_len > 64)
            printf("       ... (truncated, %d more bytes)\n", payload_len - 64);
    }
}

/* ARP 包解析与显示 */
static void
handle_arp(const unsigned char *packet, int len)
{
    if (len < (int)(ETH_HLEN + (int)sizeof(struct arp_hdr)))
        return;
    if (!should_display_packet("arp"))
        return;

    const struct arp_hdr *arp = (const struct arp_hdr *)(packet + ETH_HLEN);

    unsigned short oper = tlibc_ntohs(arp->oper);
    const char *oper_str;
    if (oper == 1)      oper_str = "Request";
    else if (oper == 2) oper_str = "Reply";
    else                oper_str = "Other";

    char ts[24], spa_str[16], tpa_str[16], sha_str[18], tha_str[18];
    timestamp_str_buf(ts, sizeof(ts));
    ip_to_str_buf(arp->spa, spa_str, sizeof(spa_str));
    ip_to_str_buf(arp->tpa, tpa_str, sizeof(tpa_str));
    mac_to_str_buf(arp->sha, sha_str, sizeof(sha_str));
    mac_to_str_buf(arp->tha, tha_str, sizeof(tha_str));

    printf("[%s] ARP: %s | %s (%s) -> %s (%s)\n",
           ts, oper_str,
           spa_str, sha_str,
           tpa_str, tha_str);
}

/* IPv4 包分发 */
static void
handle_ipv4(const unsigned char *packet, int len)
{
    if (len < (int)(ETH_HLEN + (int)sizeof(struct ip_hdr)))
        return;

    const struct ip_hdr *ip = (const struct ip_hdr *)(packet + ETH_HLEN);
    if ((ip->ver_ihl >> 4) != 4)
        return;

    int ip_hdr_len;
    print_eth((const struct eth_hdr *)packet);
    print_ip(ip, &ip_hdr_len);

    int remaining = len - ETH_HLEN - ip_hdr_len;
    if (remaining < 0) return;

    switch (ip->proto) {
        case IPPROTO_TCP:
            handle_tcp(packet, ip_hdr_len, remaining, ip);
            break;
        case IPPROTO_UDP:
            handle_udp(packet, ip_hdr_len, remaining, ip);
            break;
        case IPPROTO_ICMP:
            handle_icmp(packet, ip_hdr_len, remaining);
            break;
        default:
            if (should_display_packet("ip")) {
                char ts[24];
                timestamp_str_buf(ts, sizeof(ts));
                printf("[%s] IP:  proto=%d, remaining=%d bytes\n",
                       ts, ip->proto, remaining);
            }
            break;
    }
}

/* ================================================================== */
/*  主循环                                                             */
/* ================================================================== */

static void
print_usage(void)
{
    printf("Usage: sniffer [protocol] [port]\n");
    printf("\n");
    printf("Protocol filter: tcp, udp, icmp, arp, ip (default: all)\n");
    printf("Port filter:     valid with tcp or udp only\n");
    printf("\n");
    printf("Examples:\n");
    printf("  sniffer           # capture all packets\n");
    printf("  sniffer tcp       # TCP only\n");
    printf("  sniffer udp 53    # DNS traffic\n");
    printf("  sniffer tcp 80    # HTTP traffic\n");
    printf("  sniffer icmp      # ICMP only (pings)\n");
    printf("  sniffer arp       # ARP only\n");
}

int main(int argc, char *argv[])
{
    g_filter_proto = NULL;
    g_filter_port  = -1;

    printf("=== Tinylibc Network Sniffer ===\n");

    /* 解析命令行 */
    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }
        g_filter_proto = argv[1];

        if (argc >= 3) {
            int port = 0;
            char *p = argv[2];
            while (*p) {
                if (*p < '0' || *p > '9') {
                    printf("Invalid port: %s\n", argv[2]);
                    return 1;
                }
                port = port * 10 + (*p - '0');
                p++;
            }
            if (port < 1 || port > 65535) {
                printf("Port must be 1-65535\n");
                return 1;
            }
            g_filter_port = port;
        }
    }

    printf("Filter: %s\n", g_filter_proto ? g_filter_proto : "all");
    if (g_filter_port > 0)
        printf("Port:   %d\n", g_filter_port);
    printf("Press Ctrl+C to stop.\n\n");

    /* 创建 AF_PACKET 原始套接字 */
    int sock = socket(AF_PACKET, SOCK_RAW, tlibc_htons(ETH_P_ALL));
    if (sock < 0) {
        printf("socket(AF_PACKET, SOCK_RAW) failed (errno=%d)\n", -sock);
        printf("This program requires root privileges (CAP_NET_RAW).\n");
        return 1;
    }

    /* 捕获主循环 */
    unsigned char buf[MAX_PACKET_SIZE];
    struct sockaddr_ll sll;
    unsigned int sll_len;
    int packet_count = 0;

    while (1) {
        sll_len = sizeof(sll);
        int n = __recvfrom(sock, buf, sizeof(buf), 0, &sll, &sll_len);
        if (n < 0) {
            if (-n == EINTR)
                continue;
            printf("recvfrom() failed (errno=%d)\n", -n);
            break;
        }
        if (n < ETH_HLEN)
            continue;

        packet_count++;

        const struct eth_hdr *eth = (const struct eth_hdr *)buf;
        unsigned short etype = tlibc_ntohs(eth->type);

        int printed = 1;
        switch (etype) {
            case ETH_P_IP:
                handle_ipv4(buf, n);
                break;
            case ETH_P_ARP:
                handle_arp(buf, n);
                break;
            default:
                if (!g_filter_proto) {
                    char ts[24], src_mac[18], dst_mac[18];
                    timestamp_str_buf(ts, sizeof(ts));
                    mac_to_str_buf(eth->src, src_mac, sizeof(src_mac));
                    mac_to_str_buf(eth->dst, dst_mac, sizeof(dst_mac));
                    printf("[%s] ETH: %s -> %s | Type: 0x%x | Len: %d\n",
                           ts, src_mac, dst_mac,
                           etype, n);
                } else
                    printed = 0;
                break;
        }

        if (printed)
            printf("\n");

        if (packet_count % 1000 == 0)
            printf("--- Captured %d packets ---\n", packet_count);
    }

    close(sock);
    printf("\n=== Sniffer stopped (%d packets captured) ===\n", packet_count);
    return 0;
}
