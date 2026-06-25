/* SPDX-License-Identifier: MIT
 *
 * ndiscover — 网络邻居发现 (Network Neighbor Discovery)
 *
 * 利用 ARP 协议主动探测同一网段的所有活跃主机，显示其 IP 和 MAC 地址。
 * 先批量发送 ARP 请求，再统一监听回复，高效一次性完成全网段扫描。
 *
 * 用法:
 *   ndiscover                        # 自动检测接口并扫描

 *   ndiscover -i eth0                # 扫描指定接口所在网段
 *   ndiscover -r 10.0.0.0/24        # 扫描指定网段（需同时指定 -i）
 *   ndiscover -i eth0 -t 3000       # 设置回复等待超时 (ms)
 *
 * 示例输出:
 *   [+] Active hosts on 192.168.1.0/24 (254 hosts):
 *       192.168.1.1     (00:11:22:33:44:55) [Unknown]
 *       192.168.1.100   (aa:bb:cc:dd:ee:ff) [This host]
 *       192.168.1.101   (11:22:33:44:55:66) [Unknown]
 *     => 3 hosts found (sent 254 probes, 2000ms timeout)
 *
 * 注意: 需要 root 权限 (CAP_NET_RAW) 运行。
 *
 * 编译: make all 或 tmake 自动构建
 */

#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"
#include "syscall.h"
#include "syscall_num.h"

/* ================================================================== */
/*  Linux 网络常量 — 项目头文件未提供                                  */
/* ================================================================== */

#define AF_PACKET               17
#define ETH_P_ARP               0x0806
#define ETH_P_IP                0x0800
#define ETH_ALEN                6
#define ETH_HLEN                14

#define ARPOP_REQUEST           1
#define ARPOP_REPLY             2

/* ================================================================== */
/*  应用常量                                                           */
/* ================================================================== */

#define DEFAULT_TIMEOUT_MS      2000     /* 等待 ARP 回复超时 (ms)   */
#define MAX_TARGETS             65536    /* 最大扫描主机数 (/16)      */
#define MAX_RESULTS             4096     /* 结果记录上限              */

/* ================================================================== */
/*  IOCTL 请求码 — 与 Linux <linux/sockios.h> 一致                    */
/* ================================================================== */

#define SIOCGIFADDR             0x8915   /* 获取接口 IP               */
#define SIOCGIFNETMASK          0x891b   /* 获取接口子网掩码         */
#define SIOCGIFINDEX            0x8933   /* 获取接口索引              */
#define IFNAMSIZ                16       /* 接口名称最大长度          */

/* ================================================================== */
/*  poll 常量 — 项目未提供封装，在此定义                               */
/* ================================================================== */

#ifndef POLLIN
#define POLLIN    0x001
#define POLLERR   0x008
#define POLLHUP   0x010
#define POLLNVAL  0x020
#endif

/* ================================================================== */
/*  结构体定义                                                         */
/* ================================================================== */

/* sockaddr_ll — AF_PACKET 地址结构（项目未提供） */
struct sockaddr_ll {
    unsigned short sll_family;
    unsigned short sll_protocol;
    int            sll_ifindex;
    unsigned short sll_hatype;
    unsigned char  sll_pkttype;
    unsigned char  sll_halen;
    unsigned char  sll_addr[8];
};

/* Ethernet 头部 */
struct eth_hdr {
    unsigned char  dst[ETH_ALEN];
    unsigned char  src[ETH_ALEN];
    unsigned short type;                /* 网络字节序               */
} __attribute__((packed));

/* ARP 头部 */
struct arp_hdr {
    unsigned short htype;               /* 硬件类型（网络字节序）   */
    unsigned short ptype;               /* 协议类型（网络字节序）   */
    unsigned char  hlen;                /* 硬件地址长度            */
    unsigned char  plen;                /* 协议地址长度            */
    unsigned short oper;                /* 操作码（网络字节序）    */
    unsigned char  sha[ETH_ALEN];       /* 发送方 MAC              */
    unsigned int   spa;                 /* 发送方 IP（网络字节序） */
    unsigned char  tha[ETH_ALEN];       /* 目标 MAC                */
    unsigned int   tpa;                 /* 目标 IP（网络字节序）   */
} __attribute__((packed));

/* 完整 ARP 包（Ethernet + ARP） */
struct arp_pkt {
    struct eth_hdr eth;
    struct arp_hdr arp;
} __attribute__((packed));

/* ifreq — 用于 ioctl 查询接口信息 (对齐内核 40 字节结构) */
struct ifreq {
    char           ifr_name[IFNAMSIZ];
    unsigned char  ifr_ifru[24];
};

/* pollfd — 用于等待数据到达 */
struct pollfd {
    int   fd;
    short events;
    short revents;
};

/* 探测到的主机信息 */
struct host_info {
    unsigned int   ip;                  /* 网络字节序               */
    unsigned char  mac[ETH_ALEN];
    int            is_self;             /* 是否本机                */
};

/* ================================================================== */
/*  辅助函数                                                           */
/* ================================================================== */

/* 32 位字节序交换（x86_64 LE 下 htonl = ntohl = byte swap） */
static unsigned int
swap32(unsigned int val)
{
    unsigned char *b = (unsigned char *)&val;
    return ((unsigned int)b[0] << 24) |
           ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8)  |
           ((unsigned int)b[3]);
}

/* 将 MAC 地址格式化为 "xx:xx:xx:xx:xx:xx" */
static void
mac_to_str(const unsigned char *mac, char *buf, int buf_size)
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

/* 解析字符串为无符号整数，返回 -1 表示无效 */
static int
parse_uint(const char *s, int max_val)
{
    if (!s || !*s) return -1;
    int n = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        n = n * 10 + (*s - '0');
        if (n > max_val) return -1;
        s++;
    }
    return n;
}

/* ================================================================== */
/*  原始 socket 系统调用封装                                           */
/* ================================================================== */

static int
__sendto(int fd, const void *buf, unsigned long len, int flags,
         const struct sockaddr_ll *dest_addr, unsigned int addrlen)
{
    return syscall(__NR_sendto, fd, buf, len, flags, dest_addr, addrlen);
}

static int
__recvfrom(int fd, void *buf, unsigned long len, int flags,
           struct sockaddr_ll *src_addr, unsigned int *addrlen)
{
    return syscall(__NR_recvfrom, fd, buf, len, flags, src_addr, addrlen);
}

static int
__poll(struct pollfd *fds, unsigned long nfds, int timeout_ms)
{
    return syscall(__NR_poll, fds, nfds, timeout_ms);
}

/* ================================================================== */
/*  接口信息获取                                                       */
/* ================================================================== */

/* 读取 /sys/class/net/<iface>/address 获取本机 MAC */
static int
get_local_mac(const char *iface, unsigned char *mac)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    path[sizeof(path) - 1] = '\0';

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    char buf[20];
    int n = __read(fd, buf, sizeof(buf) - 1);
    __close(fd);
    if (n < 17)   /* "xx:xx:xx:xx:xx:xx\n" 至少 17 字节 */
        return -1;
    buf[n] = '\0';

    /* 手动解析十六进制 "xx:xx:xx:xx:xx:xx" */
    for (int i = 0; i < 6; i++) {
        unsigned int byte = 0;
        char *p = buf + i * 3;
        for (int j = 0; j < 2; j++) {
            byte <<= 4;
            char c = p[j];
            if      (c >= '0' && c <= '9') byte |= (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') byte |= (unsigned int)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') byte |= (unsigned int)(c - 'A' + 10);
            else return -1;
        }
        mac[i] = (unsigned char)byte;
    }
    return 0;
}

/* 通过 ioctl(SIOCGIFADDR) 获取本机 IP */
static int
get_local_ip(const char *iface, unsigned int *ip)
{
    struct ifreq ifr;
    __memset(ifr.ifr_name, 0, IFNAMSIZ);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    int ret = __ioctl(sock, SIOCGIFADDR, &ifr);
    if (ret == 0) {
        struct sockaddr *sa = (struct sockaddr *)ifr.ifr_ifru;
        *ip = ((struct sockaddr_in *)sa)->sin_addr.s_addr;
    }
    __close(sock);
    return ret;
}

/* 通过 ioctl(SIOCGIFNETMASK) 获取子网掩码 */
static int
get_local_netmask(const char *iface, unsigned int *netmask)
{
    struct ifreq ifr;
    __memset(ifr.ifr_name, 0, IFNAMSIZ);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    int ret = __ioctl(sock, SIOCGIFNETMASK, &ifr);
    if (ret == 0) {
        struct sockaddr *sa = (struct sockaddr *)ifr.ifr_ifru;
        *netmask = ((struct sockaddr_in *)sa)->sin_addr.s_addr;
    }
    __close(sock);
    return ret;
}

/* 通过 ioctl(SIOCGIFINDEX) 获取接口索引 */
static int
get_ifindex(const char *iface)
{
    struct ifreq ifr;
    __memset(ifr.ifr_name, 0, IFNAMSIZ);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    int ret = __ioctl(sock, SIOCGIFINDEX, &ifr);
    __close(sock);
    if (ret < 0)
        return -1;

    return *(int *)ifr.ifr_ifru;
}

/* 自动检测默认接口：在 /sys/class/net/ 中找第一个非 loopback 且有 IP 的接口 */
static int
find_default_interface(char *buf, int buf_size)
{
    int fd = __openat(AT_FDCWD, "/sys/class/net", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return -1;

    char dent_buf[4096];
    int n = __getdents64(fd, (struct linux_dirent64 *)dent_buf, sizeof(dent_buf));
    __close(fd);
    if (n <= 0)
        return -1;

    struct linux_dirent64 *de;
    int offset = 0;
    int first_candidate = -1;
    char first_name[IFNAMSIZ];

    while (offset < n) {
        de = (struct linux_dirent64 *)(dent_buf + offset);
        offset += de->d_reclen;

        if (de->d_name[0] == '.')
            continue;
        /* 跳过虚拟环回接口 lo */
        if (strcmp(de->d_name, "lo") == 0)
            continue;

        /* 记录第一个非 loopback 候选（兜底用） */
        if (first_candidate < 0) {
            snprintf(first_name, sizeof(first_name), "%s", de->d_name);
            first_name[sizeof(first_name) - 1] = '\0';
            first_candidate = 0;
        }

        /* 优先选择有 IP 地址的接口 */
        unsigned int ip;
        if (get_local_ip(de->d_name, &ip) == 0) {
            snprintf(buf, buf_size, "%s", de->d_name);
            buf[buf_size - 1] = '\0';
            return 0;
        }
    }

    /* 都没有 IP，回退到第一个非 loopback 接口（可能后续报错） */
    if (first_candidate >= 0) {
        snprintf(buf, buf_size, "%s", first_name);
        buf[buf_size - 1] = '\0';
        return 0;
    }

    return -1;
}

/* ================================================================== */
/*  CIDR 解析                                                          */
/* ================================================================== */

/*
 * 解析 "a.b.c.d/prefix" 格式的 CIDR 字符串。
 * ip_out 和 mask_out 均为网络字节序。
 */
static int
parse_cidr(const char *s, unsigned int *ip_out, unsigned int *mask_out)
{
    char ip_str[20];
    int prefix = 24;
    int pos = 0;

    /* 提取 IP 部分 */
    while (*s && *s != '/' && pos < (int)sizeof(ip_str) - 1)
        ip_str[pos++] = *s++;
    ip_str[pos] = '\0';

    /* 提取前缀长度 */
    if (*s == '/') {
        s++;
        prefix = 0;
        while (*s) {
            if (*s < '0' || *s > '9') return -1;
            prefix = prefix * 10 + (*s - '0');
            if (prefix > 32) return -1;
            s++;
        }
    }

    /* 限制有效前缀范围：/1 ~ /30，避免 /0 (UB) 和 /32 (无主机) */
    if (prefix < 1 || prefix > 30) {
        printf("Invalid prefix /%d (must be 1-30)\n", prefix);
        return -1;
    }

    unsigned int ip = tlibc_inet_addr(ip_str);
    if (ip == 0xffffffff)
        return -1;

    /* 根据前缀计算掩码 — swap32 确保掩码字节序与 tlibc_inet_addr / 内核 s_addr 一致 */
    unsigned int mask = 0;
    if (prefix > 0)
        mask = swap32(0xffffffffu << (32 - prefix));

    *ip_out = ip;
    *mask_out = mask;
    return 0;
}

/* ================================================================== */
/*  主机信息排序（插入排序 — 结果数通常 < 254 足够快）                 */
/* ================================================================== */

static void
sort_hosts(struct host_info *hosts, int count)
{
    for (int i = 1; i < count; i++) {
        struct host_info key = hosts[i];
        unsigned int key_ip = swap32(key.ip);
        int j = i - 1;
        while (j >= 0 && swap32(hosts[j].ip) > key_ip) {
            hosts[j + 1] = hosts[j];
            j--;
        }
        hosts[j + 1] = key;
    }
}

/* ================================================================== */
/*  OUI 厂商表（MAC 前 3 字节 → 厂商名）                                */
/* ================================================================== */

struct oui_entry {
    unsigned char prefix[3];
    const char   *vendor;
};

static const struct oui_entry oui_table[] = {
    {{0x00, 0x15, 0x5d}, "Microsoft"},
    {{0x00, 0x0c, 0x29}, "VMware"},
    {{0x00, 0x50, 0x56}, "VMware"},
    {{0x08, 0x00, 0x27}, "Oracle"},
    {{0x00, 0x1a, 0x11}, "Google"},
    {{0x00, 0x23, 0x93}, "Google"},
    {{0x00, 0x24, 0x97}, "Apple"},
    {{0x00, 0x25, 0x00}, "Apple"},
    {{0x00, 0x23, 0x32}, "Apple"},
    {{0x3c, 0x22, 0xfb}, "Dell"},
    {{0x00, 0x26, 0xb9}, "Dell"},
    {{0x00, 0x15, 0xc5}, "HP"},
    {{0x00, 0x1a, 0x4b}, "HP"},
    {{0x00, 0x19, 0xbb}, "Samsung"},
    {{0x00, 0x23, 0xd4}, "Samsung"},
    {{0x00, 0x12, 0x47}, "Intel"},
    {{0x00, 0x1b, 0x21}, "Intel"},
    {{0x00, 0x1e, 0x67}, "Intel"},
    {{0x00, 0x24, 0x6c}, "TP-Link"},
    {{0x00, 0x14, 0xbf}, "TP-Link"},
    {{0x00, 0x25, 0x9c}, "TP-Link"},
    {{0x00, 0x0e, 0x8e}, "Huawei"},
    {{0x00, 0x25, 0x9e}, "Huawei"},
    {{0x00, 0x18, 0x82}, "Huawei"},
    {{0x98, 0x3d, 0x79}, "Hangzhou"},
    {{0x00, 0x24, 0xbe}, "Xiaomi"},
    {{0x00, 0x16, 0xe8}, "HTC"},
    {{0x00, 0x23, 0x8b}, "Foxconn"},
    {{0x00, 0x25, 0x90}, "ASUS"},
    {{0x00, 0x26, 0x5a}, "Netgear"},
    {{0x00, 0x1b, 0xfc}, "Nokia"},
    {{0x00, 0x23, 0xcd}, "TP-Link"},
    {{0x00, 0x24, 0xd7}, "Intel"},
    {{0x00, 0x26, 0x08}, "Apple"},
    {{0x00, 0x26, 0x4a}, "Apple"},
};

/* 通过 MAC 前 3 字节查找厂商名，未找到返回 NULL */
static const char *lookup_vendor(const unsigned char *mac)
{
    int n = sizeof(oui_table) / sizeof(oui_table[0]);
    for (int i = 0; i < n; i++) {
        if (mac[0] == oui_table[i].prefix[0] &&
            mac[1] == oui_table[i].prefix[1] &&
            mac[2] == oui_table[i].prefix[2])
            return oui_table[i].vendor;
    }
    return NULL;
}

/* ================================================================== */
/*  ARP 扫描核心                                                      */
/* ================================================================== */

static int
arp_scan(const char *iface, unsigned int src_ip,
         const unsigned char *src_mac,
         unsigned int network, unsigned int netmask,
         int timeout_ms,
         struct host_info *results, int max_results,
         int *sent_out)
{
    /* ---- 获取接口索引 ---- */
    int ifindex = get_ifindex(iface);
    if (ifindex < 0) {
        printf("Error: cannot get interface index for '%s'\n", iface);
        return -1;
    }

    /* ---- 创建 AF_PACKET 原始套接字 ---- */
    int sock = socket(AF_PACKET, SOCK_RAW, tlibc_htons(ETH_P_ARP));
    if (sock < 0) {
        printf("Error: socket(AF_PACKET, SOCK_RAW) failed (errno=%d)\n", -sock);
        printf("This program requires root privileges (CAP_NET_RAW).\n");
        return -1;
    }

    /* ---- 计算扫描范围 ---- */
    unsigned int net_addr = network & netmask;
    unsigned int broadcast = net_addr | ~netmask;
    unsigned int num_hosts = swap32(~netmask) - 1;   /* 去掉网络地址和广播地址 */

    if (num_hosts > MAX_TARGETS)
        num_hosts = MAX_TARGETS;

    /* 广播 MAC */
    unsigned char broadcast_mac[ETH_ALEN];
    __memset(broadcast_mac, 0xff, ETH_ALEN);

    /* 存放 ARP 请求包（复用缓冲区） */
    struct arp_pkt pkt;
    int sent = 0;

    /* ---- 构造并发送所有 ARP 请求 ---- */
    printf("  Sending %u probes ", num_hosts);
    for (unsigned int i = 1; i <= num_hosts; i++) {
        if (i % 1024 == 0) printf(".");   /* 每 1024 个打一个点 */
        unsigned int target_ip = swap32(swap32(net_addr) + i);

        /* 跳过广播地址 */
        if (target_ip == broadcast)
            continue;

        /* 跳过本机（不等待自己的回复） */
        if (target_ip == src_ip)
            continue;

        /* 构造 ARP 请求 */
        __memmove(pkt.eth.dst, broadcast_mac, ETH_ALEN);
        __memmove(pkt.eth.src, src_mac, ETH_ALEN);
        pkt.eth.type = tlibc_htons(ETH_P_ARP);

        pkt.arp.htype = tlibc_htons(1);          /* Ethernet */
        pkt.arp.ptype = tlibc_htons(ETH_P_IP);   /* IPv4 */
        pkt.arp.hlen  = ETH_ALEN;
        pkt.arp.plen  = 4;
        pkt.arp.oper  = tlibc_htons(ARPOP_REQUEST);
        __memmove(pkt.arp.sha, src_mac, ETH_ALEN);
        pkt.arp.spa   = src_ip;
        __memset(pkt.arp.tha, 0, ETH_ALEN);
        pkt.arp.tpa   = target_ip;

        /* 用 sockaddr_ll 指定目标接口和 MAC */
        struct sockaddr_ll sll;
        __memset(&sll, 0, sizeof(sll));
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = tlibc_htons(ETH_P_ARP);
        sll.sll_ifindex  = ifindex;
        sll.sll_halen    = ETH_ALEN;
        __memmove(sll.sll_addr, broadcast_mac, ETH_ALEN);

        int ret = __sendto(sock, &pkt, sizeof(pkt), 0, &sll, sizeof(sll));
        if (ret >= 0)
            sent++;
    }
    printf("\n");

    if (sent_out)
        *sent_out = sent;

    /* ---- 记录本机 ---- */
    int result_count = 1;
    results[0].ip      = src_ip;
    __memmove(results[0].mac, src_mac, ETH_ALEN);
    results[0].is_self = 1;

    /* ---- 监听 ARP 回复 ---- */
    struct pollfd pfd;
    pfd.fd      = sock;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    unsigned char buf[sizeof(struct arp_pkt) + 64];

    while (result_count < max_results - 1) {
        pfd.revents = 0;
        int ret = __poll(&pfd, 1, timeout_ms);

        if (ret < 0) {
            if (ret == -EINTR)
                continue;
            break;
        }
        if (ret == 0)
            break;       /* 超时，结束扫描 */

        struct sockaddr_ll sll;
        unsigned int sll_len = sizeof(sll);

        int n = __recvfrom(sock, buf, sizeof(buf), 0, &sll, &sll_len);
        if (n < (int)sizeof(struct arp_pkt))
            continue;

        struct arp_pkt *rp = (struct arp_pkt *)buf;

        unsigned short etype = tlibc_ntohs(rp->eth.type);
        if (etype != ETH_P_ARP)
            continue;

        unsigned short oper = tlibc_ntohs(rp->arp.oper);
        if (oper != ARPOP_REPLY)
            continue;

        unsigned int sender_ip = rp->arp.spa;

        /* 忽略本机回复 */
        if (sender_ip == src_ip)
            continue;

        /* 检查是否已记录 */
        int already = 0;
        for (int i = 0; i < result_count; i++) {
            if (results[i].ip == sender_ip) {
                already = 1;
                break;
            }
        }
        if (already)
            continue;

        /* 记录新主机 */
        results[result_count].ip      = sender_ip;
        __memmove(results[result_count].mac, rp->arp.sha, ETH_ALEN);
        results[result_count].is_self = 0;
        {
            char found_ip[16], found_mac[18];
            ip_to_str(sender_ip, found_ip, sizeof(found_ip));
            mac_to_str(rp->arp.sha, found_mac, sizeof(found_mac));
            printf("  + %-15s (%s)\n", found_ip, found_mac);
        }
        result_count++;
    }

    close(sock);
    return result_count;
}

/* ================================================================== */
/*  结果显示                                                           */
/* ================================================================== */

static void
print_results(struct host_info *results, int count,
              unsigned int network, unsigned int netmask,
              int sent, int timeout_ms)
{
    char ip_str[16], net_str[16], mask_str[16];
    ip_to_str(network, net_str, sizeof(net_str));
    ip_to_str(netmask, mask_str, sizeof(mask_str));

    int prefix = 0;
    unsigned int m = swap32(netmask);
    while (m & 0x80000000u) { prefix++; m <<= 1; }

    unsigned int total_hosts = swap32(~netmask) - 1;

    printf("%s[+] Active hosts on %s/%d (%u hosts):%s\n",
           CYAN_COLOR_PRINT, net_str, prefix,
           total_hosts > MAX_TARGETS ? MAX_TARGETS : total_hosts,
           COLOR_RESET);

    if (count == 0) {
        printf("    (no hosts found)\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        char ip_s[16], mac_s[18];
        ip_to_str(results[i].ip, ip_s, sizeof(ip_s));
        mac_to_str(results[i].mac, mac_s, sizeof(mac_s));

        const char *color;
        const char *tag;
        if (results[i].is_self) {
            color = BRIGHT_GREEN_COLOR_PRINT;
            tag   = "This host";
        } else {
            const char *vendor = lookup_vendor(results[i].mac);
            color = vendor ? COLOR_RESET : YELLOW_COLOR_PRINT;
            tag   = vendor ? vendor : "Active";
        }

        printf("    %s%-15s (%s) [%s]%s\n",
               color, ip_s, mac_s, tag, COLOR_RESET);
    }

    printf("=> %s%d%s host%s found (sent %d probes, %dms timeout)\n",
           count > 1 ? BRIGHT_GREEN_COLOR_PRINT : YELLOW_COLOR_PRINT,
           count, COLOR_RESET,
           count > 1 ? "s" : "",
           sent, timeout_ms);
}

/* ================================================================== */
/*  用法信息                                                           */
/* ================================================================== */

static void
print_usage(void)
{
    printf("Usage: ndiscover [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -i <interface>    Network interface (default: auto-detect)\n");
    printf("  -r <cidr>         Scan range (e.g. 192.168.1.0/24)\n");
    printf("  -t <timeout_ms>   ARP reply timeout in ms (default: %d)\n",
           DEFAULT_TIMEOUT_MS);
    printf("  -h, --help        Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  ndiscover                     # Auto-detect and scan\n");
    printf("  ndiscover -i eth0             # Scan via eth0\n");
    printf("  ndiscover -i eth0 -r 10.0.0.0/24\n");
    printf("  ndiscover -t 3000             # Longer timeout\n");
    printf("\n");
    printf("Note: requires root (CAP_NET_RAW) for raw socket.\n");
}

/* ================================================================== */
/*  主函数                                                             */
/* ================================================================== */

int main(int argc, char *argv[])
{
    const char *iface     = NULL;
    int         timeout   = DEFAULT_TIMEOUT_MS;
    int         have_cidr = 0;
    unsigned int cidr_ip  = 0;
    unsigned int cidr_mask = 0;

    /* ---- 解析命令行参数 ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iface = argv[++i];
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            if (parse_cidr(argv[++i], &cidr_ip, &cidr_mask) < 0) {
                printf("Invalid CIDR: %s (expected a.b.c.d/prefix)\n", argv[i]);
                return 1;
            }
            have_cidr = 1;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            timeout = parse_uint(argv[++i], 60000);
            if (timeout < 50) {
                printf("Invalid timeout: %s (must be 50-60000)\n", argv[i]);
                return 1;
            }
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    /* ---- 自动检测接口 ---- */
    char auto_iface[IFNAMSIZ];
    if (!iface) {
        if (find_default_interface(auto_iface, sizeof(auto_iface)) < 0) {
            printf("Error: cannot detect network interface.\n");
            printf("Please specify one with: ndiscover -i <interface>\n");
            return 1;
        }
        iface = auto_iface;
    }

    printf("Interface: %s\n", iface);

    /* ---- 获取本机 MAC ---- */
    unsigned char local_mac[ETH_ALEN];
    if (get_local_mac(iface, local_mac) < 0) {
        printf("Error: cannot read MAC address for interface '%s'\n", iface);
        return 1;
    }
    char mac_str[18];
    mac_to_str(local_mac, mac_str, sizeof(mac_str));
    printf("Local MAC: %s\n", mac_str);

    /* ---- 获取本机 IP ---- */
    unsigned int local_ip;
    if (get_local_ip(iface, &local_ip) < 0) {
        printf("Error: cannot get IP address for interface '%s'\n", iface);
        printf("Is the interface configured?\n");
        return 1;
    }
    char ip_str[16];
    ip_to_str(local_ip, ip_str, sizeof(ip_str));
    printf("Local IP:  %s\n", ip_str);

    /* ---- 确定扫描范围 ---- */
    unsigned int network, netmask;

    if (have_cidr) {
        /* 用户指定了 CIDR */
        netmask = cidr_mask;
        network = cidr_ip & netmask;
    } else {
        /* 自动从接口获取 */
        if (get_local_netmask(iface, &netmask) < 0) {
            printf("Error: cannot get netmask for interface '%s'\n", iface);
            return 1;
        }
        network = local_ip & netmask;
    }

    char net_str[16], mask_str[16];
    ip_to_str(network, net_str, sizeof(net_str));
    ip_to_str(netmask, mask_str, sizeof(mask_str));
    printf("Network:   %s (mask %s)\n\n", net_str, mask_str);

    /* ---- 执行 ARP 扫描 ---- */
    struct host_info results[MAX_RESULTS];
    int sent = 0;
    int n = arp_scan(iface, local_ip, local_mac,
                     network, netmask, timeout,
                     results, MAX_RESULTS, &sent);

    if (n < 0)
        return 1;

    /* ---- 排序并显示结果 ---- */
    sort_hosts(results, n);
    print_results(results, n, network, netmask, sent, timeout);

    return 0;
}
