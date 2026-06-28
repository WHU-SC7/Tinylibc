/* SPDX-License-Identifier: MIT
 *
 * ndiscover — 网络邻居发现 (Network Neighbor Discovery)
 *
 * 利用 ARP 主动探测同一网段活跃主机，结合 ICMP TTL、TCP 端口探测
 * 和 OUI 厂商数据库进行多信号设备类型识别。
 *
 * 设备分类（优先级）:
 *   1. OUI 厂商精确匹配（Xiaomi → Phone, TP-Link → Router）
 *   2. 默认网关判断（网关 IP 一定是 Router）
 *   3. 随机 MAC + TTL → Phone Android / Phone iOS
 *   4. TTL 值 → PC Linux / PC Windows / Router RTOS
 *   5. 开放端口 → Printer / Camera / IoT / NAS
 * 输出格式: [设备类型] [系统/厂商/描述] [开放端口]
 *
 * TCP 端口探测（默认开启）:
 *   22=SSH, 80=HTTP, 443=HTTPS, 554=RTSP(camera),
 *   8080=HTTP-alt, 1883=MQTT(IoT), 9100=Printer
 *   使用 --fast 跳过（仅 ARP+ICMP）
 *
 * 内置常见厂商 OUI 表（TP-Link、Apple、Intel 等 18 条），也可通过
 * --oui-file 加载自定义 OUI 文件辅助识别。
 *
 * 用法:
 *   ndiscover                               # 默认全模式（ARP+ICMP+端口）
 *   ndiscover -i eth0                       # 指定接口
 *   ndiscover -r 10.0.0.0/24               # 指定网段
 *   ndiscover -i eth0 -t 3000              # 设置超时 (ms)
 *   ndiscover --fast                        # 跳过端口探测（快速模式）
 *   ndiscover --oui-file /path/to/oui.txt  # 加载自定义 OUI 文件
 *
 * 权限: 需要 CAP_NET_RAW（ARP / ICMP 使用 AF_PACKET 原始套接字）
 *       sudo setcap cap_net_raw+ep ~/tlibc/bin/ndiscover
 *
 * 注意: 需要 CAP_NET_RAW 权限。Fix once: sudo setcap cap_net_raw+ep ~/tlibc/bin/ndiscover
 *
 * 编译: tmake -b ndiscover
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
/*  Linux 网络常量 — 项目头文件未提供 */
/* ================================================================== */

#define AF_PACKET               17
#define ETH_P_ARP               0x0806
#define ETH_P_IP                0x0800
#define ETH_ALEN                6
#define ETH_HLEN                14

#define ARPOP_REQUEST           1
#define ARPOP_REPLY             2

#define IPPROTO_ICMP            1
#define ICMP_ECHO               8
#define ICMP_ECHOREPLY          0

/* ================================================================== */
/*  应用常量 */
/* ================================================================== */

#define DEFAULT_TIMEOUT_MS      2000     /* 等待 ARP 回复超时 (ms) */
#define MAX_TARGETS             65536    /* 最大扫描主机数 (/16) */
#define MAX_RESULTS             4096     /* 结果记录上限 */
#define PORT_SCAN_TIMEOUT       800      /* 端口探测单批超时 (ms) */

/* ================================================================== */
/*  IOCTL 请求码 */
/* ================================================================== */

#define SIOCGIFADDR             0x8915
#define SIOCGIFNETMASK          0x891b
#define SIOCGIFINDEX            0x8933
#define IFNAMSIZ                16

/* ================================================================== */
/*  poll 常量 */
/* ================================================================== */

#ifndef POLLIN
#define POLLIN    0x001
#define POLLOUT   0x004
#define POLLERR   0x008
#define POLLHUP   0x010
#define POLLNVAL  0x020
#endif

/* ================================================================== */
/*  结构体定义 */
/* ================================================================== */

/* sockaddr_ll — AF_PACKET 地址结构 */
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
    unsigned short type;
} __attribute__((packed));

/* ARP 头部 */
struct arp_hdr {
    unsigned short htype;
    unsigned short ptype;
    unsigned char  hlen;
    unsigned char  plen;
    unsigned short oper;
    unsigned char  sha[ETH_ALEN];
    unsigned int   spa;
    unsigned char  tha[ETH_ALEN];
    unsigned int   tpa;
} __attribute__((packed));

/* 完整 ARP 包 */
struct arp_pkt {
    struct eth_hdr eth;
    struct arp_hdr arp;
} __attribute__((packed));

/* ifreq — for ioctl(2) */
struct ifreq {
    char           ifr_name[IFNAMSIZ];
    unsigned char  ifr_ifru[24];
};

/* IPv4 头部（20 字节，无选项）*/
struct ip_hdr {
    unsigned char  ver_ihl;
    unsigned char  tos;
    unsigned short total_len;
    unsigned short id;
    unsigned short frag_off;
    unsigned char  ttl;
    unsigned char  protocol;
    unsigned short checksum;
    unsigned int   src_ip;
    unsigned int   dst_ip;
} __attribute__((packed));

/* ICMP Echo 头部 */
struct icmp_echo {
    unsigned char  type;
    unsigned char  code;
    unsigned short checksum;
    unsigned short id;
    unsigned short seq;
} __attribute__((packed));

/* ================================================================== */
/*  设备类型枚举 */
/* ================================================================== */

enum device_class {
    DEV_UNKNOWN = 0,
    DEV_PHONE,          /* 手机 / 移动设备 */
    DEV_PC,             /* PC / 笔记本    */
    DEV_ROUTER,         /* 路由器 / AP    */
    DEV_NAS,            /* NAS / 服务器   */
    DEV_IOT,            /* IoT / 智能家居 */
    DEV_PRINTER,        /* 打印机         */
    DEV_GAME,           /* 游戏机         */
    DEV_TV,             /* 电视 / 机顶盒  */
    DEV_VIRTUAL,        /* 虚拟机         */
};

/* ================================================================== */
/*  端口位定义 — 用于 host_info.open_ports 位掩码 */
/* ================================================================== */

enum port_bit {
    PORT_BIT_SSH     = 0,  /* 22 */
    PORT_BIT_HTTP    = 1,  /* 80 */
    PORT_BIT_HTTPS   = 2,  /* 443 */
    PORT_BIT_RTSP    = 3,  /* 554 */
    PORT_BIT_HTTP2   = 4,  /* 8080 */
    PORT_BIT_MQTT    = 5,  /* 1883 */
    PORT_BIT_PRINTER = 6,  /* 9100 */
};

/* 端口探测表 */
struct port_entry {
    int                port;
    unsigned int       bit;
    const char        *svc;     /* 服务名简写 */
    enum device_class  hint;    /* 开此端口暗示的设备类型 */
};

static const struct port_entry PORT_TABLE[] = {
    {22,   1u << PORT_BIT_SSH,     "SSH",     DEV_PC},
    {80,   1u << PORT_BIT_HTTP,    "HTTP",    DEV_ROUTER},
    {443,  1u << PORT_BIT_HTTPS,   "HTTPS",   DEV_ROUTER},
    {554,  1u << PORT_BIT_RTSP,    "RTSP",    DEV_IOT},
    {8080, 1u << PORT_BIT_HTTP2,   "HTTP-alt",DEV_ROUTER},
    {1883, 1u << PORT_BIT_MQTT,    "MQTT",    DEV_IOT},
    {9100, 1u << PORT_BIT_PRINTER, "Printer", DEV_PRINTER},
};
#define NUM_PORTS (sizeof(PORT_TABLE) / sizeof(PORT_TABLE[0]))

/* ================================================================== */
/*  探测到的主机信息 */
/* ================================================================== */

struct host_info {
    unsigned int   ip;                  /* 网络字节序 */
    unsigned char  mac[ETH_ALEN];
    int            is_self;
    int            ttl;                 /* ICMP TTL (0 = 未探测到) */
    unsigned int   open_ports;          /* 端口位掩码 */
    char           vendor[32];          /* 从 OUI 解析的厂商名（精确匹配）*/
    char           os_info[24];         /* 启发式推断的 OS/描述信息 */
    enum device_class dev_class;        /* 识别的设备类型        */
    int            class_signal;        /* 分类依据: 0=OUI, 1=混合启发式 */
};

/* ================================================================== */
/*  辅助函数 */
/* ================================================================== */

static unsigned int
swap32(unsigned int val)
{
    unsigned char *b = (unsigned char *)&val;
    return ((unsigned int)b[0] << 24) |
           ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8)  |
           ((unsigned int)b[3]);
}

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

/* 判断 MAC 是否为本地管理地址（随机 MAC 的标志）*/
static int
is_random_mac(const unsigned char *mac)
{
    return (mac[0] & 0x02) != 0;
}

/* ================================================================== */
/*  原始 socket 系统调用封装 */
/* ================================================================== */

/* AF_PACKET sendto（ARP / ICMP 使用）*/
static int
__pkt_sendto(int fd, const void *buf, unsigned long len,
              const struct sockaddr_ll *addr)
{
    return syscall(__NR_sendto, fd, buf, len, 0, addr, sizeof(*addr));
}

/* 通用 recvfrom */
static int
__pkt_recvfrom(int fd, void *buf, unsigned long len,
                struct sockaddr_ll *addr, unsigned int *addrlen)
{
    return syscall(__NR_recvfrom, fd, buf, len, 0, addr, addrlen);
}

static int
__poll_wait(struct pollfd *fds, unsigned long nfds, int timeout_ms)
{
    return syscall(__NR_poll, fds, nfds, timeout_ms);
}

/* ================================================================== */
/*  接口信息获取 */
/* ================================================================== */

static int
get_local_mac(const char *iface, unsigned char *mac)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    path[sizeof(path) - 1] = '\0';

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;

    char buf[20];
    int n = __read(fd, buf, sizeof(buf) - 1);
    __close(fd);
    if (n < 17) return -1;
    buf[n] = '\0';

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

static int
get_local_ip(const char *iface, unsigned int *ip)
{
    struct ifreq ifr;
    __memset(ifr.ifr_name, 0, IFNAMSIZ);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int ret = __ioctl(sock, SIOCGIFADDR, &ifr);
    if (ret == 0) {
        struct sockaddr *sa = (struct sockaddr *)ifr.ifr_ifru;
        *ip = ((struct sockaddr_in *)sa)->sin_addr.s_addr;
    }
    __close(sock);
    return ret;
}

static int
get_local_netmask(const char *iface, unsigned int *netmask)
{
    struct ifreq ifr;
    __memset(ifr.ifr_name, 0, IFNAMSIZ);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int ret = __ioctl(sock, SIOCGIFNETMASK, &ifr);
    if (ret == 0) {
        struct sockaddr *sa = (struct sockaddr *)ifr.ifr_ifru;
        *netmask = ((struct sockaddr_in *)sa)->sin_addr.s_addr;
    }
    __close(sock);
    return ret;
}

static int
get_ifindex(const char *iface)
{
    struct ifreq ifr;
    __memset(ifr.ifr_name, 0, IFNAMSIZ);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int ret = __ioctl(sock, SIOCGIFINDEX, &ifr);
    __close(sock);
    if (ret < 0) return -1;

    return *(int *)ifr.ifr_ifru;
}

static int
find_default_interface(char *buf, int buf_size)
{
    int fd = __openat(AT_FDCWD, "/sys/class/net", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return -1;

    char dent_buf[4096];
    int n = __getdents64(fd, (struct linux_dirent64 *)dent_buf, sizeof(dent_buf));
    __close(fd);
    if (n <= 0) return -1;

    struct linux_dirent64 *de;
    int offset = 0;
    int first_candidate = -1;
    char first_name[IFNAMSIZ];

    while (offset < n) {
        de = (struct linux_dirent64 *)(dent_buf + offset);
        offset += de->d_reclen;
        if (de->d_name[0] == '.') continue;
        if (strcmp(de->d_name, "lo") == 0) continue;

        if (first_candidate < 0) {
            snprintf(first_name, sizeof(first_name), "%s", de->d_name);
            first_name[sizeof(first_name) - 1] = '\0';
            first_candidate = 0;
        }

        unsigned int ip;
        if (get_local_ip(de->d_name, &ip) == 0) {
            snprintf(buf, buf_size, "%s", de->d_name);
            buf[buf_size - 1] = '\0';
            return 0;
        }
    }

    if (first_candidate >= 0) {
        snprintf(buf, buf_size, "%s", first_name);
        buf[buf_size - 1] = '\0';
        return 0;
    }
    return -1;
}

/* ================================================================== */
/*  CIDR 解析 */
/* ================================================================== */

static int
parse_cidr(const char *s, unsigned int *ip_out, unsigned int *mask_out)
{
    char ip_str[20];
    int prefix = 24;
    int pos = 0;

    while (*s && *s != '/' && pos < (int)sizeof(ip_str) - 1)
        ip_str[pos++] = *s++;
    ip_str[pos] = '\0';

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

    if (prefix < 1 || prefix > 30) {
        printf("Invalid prefix /%d (must be 1-30)\n", prefix);
        return -1;
    }

    unsigned int ip = tlibc_inet_addr(ip_str);
    if (ip == 0xffffffff) return -1;

    unsigned int mask = 0;
    if (prefix > 0)
        mask = swap32(0xffffffffu << (32 - prefix));

    *ip_out = ip;
    *mask_out = mask;
    return 0;
}

/* ================================================================== */
/*  OUI 系统 */
/* ================================================================== */

/* 设备类型标签和颜色 */
static const char *
dev_class_label(enum device_class dc)
{
    switch (dc) {
        case DEV_PHONE:   return "Phone";
        case DEV_PC:      return "PC";
        case DEV_ROUTER:  return "Router";
        case DEV_NAS:     return "NAS";
        case DEV_IOT:     return "IoT";
        case DEV_PRINTER: return "Printer";
        case DEV_GAME:    return "Game";
        case DEV_TV:      return "TV";
        case DEV_VIRTUAL: return "VM";
        default:          return "Device";
    }
}

static const char *
dev_class_color(enum device_class dc)
{
    switch (dc) {
        case DEV_PHONE:   return BRIGHT_MAGANTA_COLOR_PRINT;
        case DEV_ROUTER:  return BRIGHT_CYAN_COLOR_PRINT;
        case DEV_PC:      return BRIGHT_BLUE_COLOR_PRINT;
        case DEV_NAS:     return BRIGHT_YELLOW_COLOR_PRINT;
        case DEV_IOT:     return COLOR_RESET;
        case DEV_GAME:    return BRIGHT_RED_COLOR_PRINT;
        case DEV_TV:      return BRIGHT_CYAN_COLOR_PRINT;
        case DEV_PRINTER: return BRIGHT_GREEN_COLOR_PRINT;
        default:          return COLOR_RESET;
    }
}

/* OUI 条目 */
struct oui_entry {
    unsigned char     prefix[3];
    const char       *vendor;
    enum device_class dev_class;
};

#define OUI_ENTRY(a,b,c,v,dc) {{a,b,c}, v, dc}
#define OUI_PHONE(a,b,c,v)   OUI_ENTRY(a,b,c,v, DEV_PHONE)
#define OUI_PC(a,b,c,v)      OUI_ENTRY(a,b,c,v, DEV_PC)
#define OUI_ROUTER(a,b,c,v)  OUI_ENTRY(a,b,c,v, DEV_ROUTER)
#define OUI_NAS(a,b,c,v)     OUI_ENTRY(a,b,c,v, DEV_NAS)
#define OUI_IOT(a,b,c,v)     OUI_ENTRY(a,b,c,v, DEV_IOT)
#define OUI_GAME(a,b,c,v)    OUI_ENTRY(a,b,c,v, DEV_GAME)
#define OUI_TV(a,b,c,v)      OUI_ENTRY(a,b,c,v, DEV_TV)
#define OUI_VIRT(a,b,c,v)    OUI_ENTRY(a,b,c,v, DEV_VIRTUAL)

/*
 * 内置 OUI 表 —— 覆盖最常见的消费级厂商。
 * 可通过 --oui-file <path> 加载自定义文件扩展。
 */
static const struct oui_entry oui_builtin[] = {
    /* --- 路由器 / 网络设备 --- */
    OUI_ROUTER(0x50, 0xc7, 0xbf, "TP-Link"),
    OUI_ROUTER(0x74, 0xda, 0xda, "TP-Link"),
    OUI_ROUTER(0x84, 0xd8, 0x1b, "TP-Link"),
    OUI_ROUTER(0xa0, 0xf3, 0xc1, "TP-Link"),
    OUI_ROUTER(0x00, 0x00, 0x0c, "Cisco"),
    OUI_ROUTER(0xec, 0x43, 0xf6, "Ralink"),
    OUI_ROUTER(0xf4, 0x7b, 0x5e, "Ralink"),

    /* --- 手机 / 移动设备 --- */
    OUI_PHONE(0x3c, 0x22, 0xfb, "Apple"),
    OUI_PHONE(0xb8, 0x53, 0xac, "Apple"),
    OUI_PHONE(0xf0, 0x18, 0x98, "Apple"),
    OUI_PHONE(0x18, 0xfb, 0x2e, "Xiaomi"),
    OUI_PHONE(0x28, 0xd2, 0x44, "Xiaomi"),
    OUI_PHONE(0x58, 0xa0, 0x6b, "Samsung"),

    /* --- PC / 网卡 --- */
    OUI_PC   (0x3c, 0x97, 0x0e, "Intel"),
    OUI_PC   (0x00, 0x1b, 0x21, "Intel"),
    OUI_PC   (0x00, 0xe0, 0x4c, "Realtek"),

    /* --- IoT --- */
    OUI_IOT  (0xb8, 0x27, 0xeb, "RPi Foundation"),
    OUI_IOT  (0x24, 0x0a, 0xc4, "Espressif"),
};

static int oui_builtin_count = sizeof(oui_builtin) / sizeof(oui_builtin[0]);

/* 动态加载的 OUI 条目（从文件读取）*/
static struct oui_entry *oui_loaded = NULL;
static int oui_loaded_count = 0;

/* 从文件加载 OUI 条目（双遍扫描：先计数，再分配+解析）*/
static struct oui_entry *
load_oui_file(const char *path, int *out_count)
{
    *out_count = 0;
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return NULL;

    struct stat st;
    if (__fstat(fd, &st) < 0 || st.st_size <= 0) { __close(fd); return NULL; }

    long file_size = st.st_size;
    char *data = (char *)tlibc_malloc((unsigned long)(file_size + 1));
    if (!data) { __close(fd); return NULL; }

    int n = __read(fd, data, file_size);
    __close(fd);
    if (n <= 0) { return NULL; }
    data[n] = '\0';

    /* 第一遍：统计有效条目数 */
    int count = 0;
    {
        char *lp = data;
        while (lp && *lp) {
            char *nl = strchr(lp, '\n');
            if (nl) *nl = '\0';

            char *p = lp;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '#' && *p != '\0' && strlen(p) > 6 && p[2] == ':' && p[5] == ':')
                count++;

            if (nl) { *nl = '\n'; lp = nl + 1; } else break;
        }
    }

    if (count == 0) { return NULL; }

    /* 分配全部内存：entries + 所有 vendor 字符串 */
    size_t vendor_pool_size = (size_t)file_size;
    char *pool = (char *)tlibc_malloc(
        (unsigned long)(count * sizeof(struct oui_entry) + vendor_pool_size));
    if (!pool) { return NULL; }

    struct oui_entry *entries = (struct oui_entry *)pool;
    char *vendor_pool = pool + count * sizeof(struct oui_entry);
    int pool_pos = 0;

    /* 第二遍：解析 */
    int idx = 0;
    char *lp = data;
    while (lp && *lp && idx < count) {
        char *nl = strchr(lp, '\n');
        if (nl) *nl = '\0';

        char *p = lp;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0') { if (nl) { *nl = '\n'; lp = nl + 1; } else break; continue; }

        /* 解析 OUI 前缀 (XX:XX:XX) */
        int oui_vals[3];
        int ok = 1;
        for (int j = 0; j < 3; j++) {
            unsigned int v = 0;
            for (int k = 0; k < 2; k++) {
                v <<= 4;
                char c = p[k];
                if      (c >= '0' && c <= '9') v |= (unsigned int)(c - '0');
                else if (c >= 'a' && c <= 'f') v |= (unsigned int)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (unsigned int)(c - 'A' + 10);
                else { ok = 0; break; }
            }
            if (!ok) break;
            oui_vals[j] = (int)v;
            if (j < 2) {
                if (p[2] != ':') { ok = 0; break; }
                p += 3;
            }
        }
        if (!ok || p[2] != '|') { if (nl) { *nl = '\n'; lp = nl + 1; } else break; continue; }

        char *vendor = p + 3;
        char *cls_str = strchr(vendor, '|');
        if (!cls_str) { if (nl) { *nl = '\n'; lp = nl + 1; } else break; continue; }
        *cls_str++ = '\0';

        /* 设备类 */
        enum device_class dc = DEV_UNKNOWN;
        int cls_int = parse_uint(cls_str, 9);
        if (cls_int >= 0 && cls_int <= 9)
            dc = (enum device_class)cls_int;

        entries[idx].prefix[0] = (unsigned char)oui_vals[0];
        entries[idx].prefix[1] = (unsigned char)oui_vals[1];
        entries[idx].prefix[2] = (unsigned char)oui_vals[2];

        /* 复制 vendor 到 pool */
        int vlen = (int)strlen(vendor);
        if (vlen > 31) vlen = 31;
        __memmove(vendor_pool + pool_pos, vendor, vlen);
        vendor_pool[pool_pos + vlen] = '\0';
        entries[idx].vendor = vendor_pool + pool_pos;
        pool_pos += vlen + 1;

        entries[idx].dev_class = dc;
        idx++;

        if (nl) { *nl = '\n'; lp = nl + 1; } else break;
    }

    if (idx == 0) {
        return NULL;
    }

    *out_count = idx;
    return entries;
}

/* 查找 OUI */
static const struct oui_entry *
lookup_oui(const unsigned char *mac)
{
    const struct oui_entry *table;
    int n;

    /* 优先使用已加载的外部 OUI 表 */
    if (oui_loaded && oui_loaded_count > 0) {
        table = oui_loaded;
        n = oui_loaded_count;
    } else {
        table = oui_builtin;
        n = oui_builtin_count;
    }

    for (int i = 0; i < n; i++) {
        if (mac[0] == table[i].prefix[0] &&
            mac[1] == table[i].prefix[1] &&
            mac[2] == table[i].prefix[2])
            return &table[i];
    }
    return NULL;
}

/* ================================================================== */
/*  多信号设备分类引擎 */
/* ================================================================== */

/*
 * 综合 OUI（厂商数据库）、ICMP TTL、TCP 端口探测、MAC 随机位
 * 进行设备类型推断。
 *
 * 策略（优先级由高到低）：
 *   1. OUI 匹配 → 直接取 OUI 定义的设备类型
 *   2. 随机 MAC + TTL=64 → Android（隐私 MAC）
 *   3. 随机 MAC + TTL=128 → iPhone/iOS（隐私 MAC）
 *   4. TTL=255 → 路由器类网络设备
 *   5. 开放端口暗示（如 80+443 → 路由器, 9100 → 打印机）
 *   6. 多个端口联合推断
 */
/*
 * 从 /proc/net/route 读取默认网关 IP。
 * 返回网络字节序的 IP，0 表示读取失败或无默认路由。
 * 格式: Iface Destination Gateway Flags ... （Destination=00000000 的条目是默认路由）
 */
static unsigned int
get_default_gateway(void)
{
    int fd = __openat(AT_FDCWD, "/proc/net/route", O_RDONLY, 0);
    if (fd < 0) return 0;

    char buf[2048];
    int n = __read(fd, buf, sizeof(buf) - 1);
    __close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* 跳过表头行 */
        if (strncmp(line, "Iface", 5) == 0) { line = nl ? nl + 1 : NULL; continue; }

        /* 解析: Iface Destination Gateway Flags ... */
        char *p = line;
        /* 跳过接口名 */
        while (*p && *p != '\t' && *p != ' ') p++;
        while (*p && (*p == '\t' || *p == ' ')) p++;

        /* Destination（十六进制，网络字节序）*/
        unsigned long dest = 0;
        {
            char *end;
            const char *hex = p;
            while (*p && *p != '\t' && *p != ' ') p++;
            while (*p && (*p == '\t' || *p == ' ')) p++;
            /* hex str to unsigned long */
            dest = 0;
            const char *h = hex;
            while (*h && *h != '\t' && *h != ' ') {
                dest <<= 4;
                char c = *h;
                if      (c >= '0' && c <= '9') dest |= (unsigned long)(c - '0');
                else if (c >= 'a' && c <= 'f') dest |= (unsigned long)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') dest |= (unsigned long)(c - 'A' + 10);
                else break;
                h++;
            }
        }

        /* 默认路由: Destination = 0 */
        if (dest != 0) { line = nl ? nl + 1 : NULL; continue; }

        /* Gateway（十六进制，网络字节序）*/
        unsigned long gw = 0;
        {
            const char *h = p;
            while (*p && *p != '\t' && *p != ' ') p++;
            /* h 指向 Gateway 十六进制串 */
            gw = 0;
            while (*h && *h != '\t' && *h != ' ') {
                gw <<= 4;
                char c = *h;
                if      (c >= '0' && c <= '9') gw |= (unsigned long)(c - '0');
                else if (c >= 'a' && c <= 'f') gw |= (unsigned long)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') gw |= (unsigned long)(c - 'A' + 10);
                else break;
                h++;
            }
        }

        /* /proc/net/route 的 Gateway 是网络字节序，直接返回 */
        return (unsigned int)gw;
    }
    return 0;
}

static void
classify_device(struct host_info *hi, unsigned int local_ip, unsigned int gateway_ip)
{
    const struct oui_entry *oui = lookup_oui(hi->mac);
    int is_self = (hi->ip == local_ip);

    hi->os_info[0] = '\0';

    if (is_self) {
        hi->dev_class = DEV_PC;
        hi->class_signal = 0;
        snprintf(hi->vendor, sizeof(hi->vendor), "This host");
        return;
    }

    int mac_random = is_random_mac(hi->mac);

    /* 信号 1: OUI 厂商匹配（最高优先级）*/
    if (oui) {
        hi->dev_class = oui->dev_class;
        hi->class_signal = 0;
        snprintf(hi->vendor, sizeof(hi->vendor), "%s", oui->vendor);
        /* OUI 已知时，额外显示 TTL 提示的 OS（有助于发现异常）*/
        if (hi->ttl > 0) {
            if (hi->ttl >= 250)
                snprintf(hi->os_info, sizeof(hi->os_info), "TTL=%d", hi->ttl);
            else if (hi->ttl >= 120)
                snprintf(hi->os_info, sizeof(hi->os_info), "Windows");
            else if (hi->ttl >= 58)
                snprintf(hi->os_info, sizeof(hi->os_info), "Linux");
        }
        return;
    }

    /* 信号 2: 默认网关 — 一定是路由器（无论跑什么 OS）*/
    if (gateway_ip && hi->ip == gateway_ip) {
        hi->class_signal = 1;
        hi->dev_class = DEV_ROUTER;
        if (hi->ttl >= 120)
            snprintf(hi->os_info, sizeof(hi->os_info), "Windows");
        else if (hi->ttl >= 58)
            snprintf(hi->os_info, sizeof(hi->os_info), "Linux");
        else if (hi->ttl >= 250)
            snprintf(hi->os_info, sizeof(hi->os_info), "RTOS");
        else
            snprintf(hi->os_info, sizeof(hi->os_info), "?");
        return;
    }

    /* 信号 3: 随机 MAC（隐私 MAC）— 绝大多数是手机 */
    if (mac_random) {
        hi->class_signal = 1;
        hi->dev_class = DEV_PHONE;
        if (hi->ttl >= 120)
            snprintf(hi->os_info, sizeof(hi->os_info), "iOS");
        else if (hi->ttl >= 58)
            snprintf(hi->os_info, sizeof(hi->os_info), "Android");
        else
            hi->os_info[0] = '\0';
        return;
    }

    /* 信号 3: 纯粹的 TTL 推断（未知 MAC 的设备）*/
    if (hi->ttl > 0) {
        hi->class_signal = 1;
        if (hi->ttl >= 250) {
            hi->dev_class = DEV_ROUTER;
            snprintf(hi->os_info, sizeof(hi->os_info), "RTOS");
        } else if (hi->ttl >= 120) {
            hi->dev_class = DEV_PC;
            snprintf(hi->os_info, sizeof(hi->os_info), "Windows");
        } else if (hi->ttl >= 58) {
            hi->dev_class = DEV_PC;
            snprintf(hi->os_info, sizeof(hi->os_info), "Linux");
        } else {
            hi->dev_class = DEV_UNKNOWN;
            snprintf(hi->os_info, sizeof(hi->os_info), "TTL=%d", hi->ttl);
        }
        return;
    }

    /* 信号 4: 开放端口推断 */
    if (hi->open_ports) {
        hi->class_signal = 1;
        int has_http  = (hi->open_ports & (1u << PORT_BIT_HTTP)) ||
                        (hi->open_ports & (1u << PORT_BIT_HTTP2));
        int has_https = hi->open_ports & (1u << PORT_BIT_HTTPS);
        int has_ssh   = hi->open_ports & (1u << PORT_BIT_SSH);
        int has_rtsp  = hi->open_ports & (1u << PORT_BIT_RTSP);
        int has_print = hi->open_ports & (1u << PORT_BIT_PRINTER);
        int has_mqtt  = hi->open_ports & (1u << PORT_BIT_MQTT);

        if (has_print)
            hi->dev_class = DEV_PRINTER;
        else if (has_rtsp)
            hi->dev_class = DEV_IOT;
        else if (has_mqtt)
            hi->dev_class = DEV_IOT;
        else if (has_http && has_https && has_ssh)
            hi->dev_class = DEV_ROUTER;
        else if (has_http && has_https)
            hi->dev_class = DEV_NAS;
        else if (has_ssh)
            hi->dev_class = DEV_NAS;
        else if (has_http)
            hi->dev_class = DEV_ROUTER;
        else
            hi->dev_class = DEV_UNKNOWN;
        return;
    }

    /* 完全未知 */
    hi->dev_class = DEV_UNKNOWN;
    hi->class_signal = 1;
}

/* ================================================================== */
/*  TCP 端口探测 */
/* ================================================================== */

/*
 * 对指定主机探测其常见端口是否开放。
 * 使用非阻塞 connect + poll 实现超时，每批探测并行进行。
 */
static void
probe_ports(struct host_info *results, int count, int timeout_ms)
{
    if (count <= 1) return;  /* 只有本机 */

    /* 按端口分批扫描：每次扫描一个端口下所有主机 */
    for (unsigned int pi = 0; pi < NUM_PORTS; pi++) {
        int port   = PORT_TABLE[pi].port;
        unsigned int bit = PORT_TABLE[pi].bit;

        /* 每批最多 64 个并行连接 — 分批迭代所有主机 */
        #define BATCH_MAX 64
        struct {
            int fd;
            int host_idx;
        } batch[BATCH_MAX];

        int next = 0;  /* 下一批开始的主机索引 */
        while (next < count) {
            int batch_count = 0;
            int i = next;

            /* 填充当前批：跳过本机，最多 BATCH_MAX 个非阻塞连接 */
            while (i < count && batch_count < BATCH_MAX) {
                if (results[i].is_self) { i++; continue; }

                int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
                if (sock < 0) { i++; continue; }

                struct sockaddr_in addr;
                __memset(&addr, 0, sizeof(addr));
                addr.sin_family      = AF_INET;
                addr.sin_port        = tlibc_htons(port);
                addr.sin_addr.s_addr = results[i].ip;

                int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
                if (ret == 0) {
                    results[i].open_ports |= bit;
                    close(sock);
                    i++;
                    continue;
                }
                if (ret != -EINPROGRESS) {
                    close(sock);
                    i++;
                    continue;
                }

                batch[batch_count].fd       = sock;
                batch[batch_count].host_idx = i;
                batch_count++;
                i++;
            }

            /* 轮询这一批 */
            if (batch_count > 0) {
                struct pollfd pfds[BATCH_MAX];
                for (int j = 0; j < batch_count; j++) {
                    pfds[j].fd      = batch[j].fd;
                    pfds[j].events  = POLLOUT;
                    pfds[j].revents = 0;
                }

                __poll_wait(pfds, batch_count, timeout_ms);

                for (int j = 0; j < batch_count; j++) {
                    if (pfds[j].revents & POLLOUT) {
                        results[batch[j].host_idx].open_ports |= bit;
                    }
                    close(batch[j].fd);
                }
            }

            next = i;  /* 前进到下一批 */
        }
        #undef BATCH_MAX
    }
}

/* ================================================================== */
/*  ARP 扫描核心 */
/* ================================================================== */

static int
arp_scan(const char *iface, unsigned int src_ip,
         const unsigned char *src_mac,
         unsigned int network, unsigned int netmask,
         int timeout_ms,
         struct host_info *results, int max_results,
         int *sent_out)
{
    int ifindex = get_ifindex(iface);
    if (ifindex < 0) {
        printf("Error: cannot get interface index for '%s'\n", iface);
        return -1;
    }

    int sock = socket(AF_PACKET, SOCK_RAW, tlibc_htons(ETH_P_ARP));
    if (sock < 0) {
        printf("Error: socket(AF_PACKET) failed (errno=%d)\n", -sock);
        printf("Need CAP_NET_RAW. Fix once: sudo setcap cap_net_raw+ep ~/tlibc/bin/ndiscover\n");
        return -1;
    }

    unsigned int net_addr = network & netmask;
    unsigned int broadcast = net_addr | ~netmask;
    unsigned int num_hosts = swap32(~netmask) - 1;

    if (num_hosts > MAX_TARGETS) num_hosts = MAX_TARGETS;

    unsigned char broadcast_mac[ETH_ALEN];
    __memset(broadcast_mac, 0xff, ETH_ALEN);

    struct arp_pkt pkt;
    int sent = 0;

    printf("  Sending %u probes ", num_hosts);
    for (unsigned int i = 1; i <= num_hosts; i++) {
        if (i % 1024 == 0) printf(".");
        unsigned int target_ip = swap32(swap32(net_addr) + i);

        if (target_ip == broadcast) continue;
        if (target_ip == src_ip) continue;

        __memmove(pkt.eth.dst, broadcast_mac, ETH_ALEN);
        __memmove(pkt.eth.src, src_mac, ETH_ALEN);
        pkt.eth.type = tlibc_htons(ETH_P_ARP);

        pkt.arp.htype = tlibc_htons(1);
        pkt.arp.ptype = tlibc_htons(ETH_P_IP);
        pkt.arp.hlen  = ETH_ALEN;
        pkt.arp.plen  = 4;
        pkt.arp.oper  = tlibc_htons(ARPOP_REQUEST);
        __memmove(pkt.arp.sha, src_mac, ETH_ALEN);
        pkt.arp.spa   = src_ip;
        __memset(pkt.arp.tha, 0, ETH_ALEN);
        pkt.arp.tpa   = target_ip;

        struct sockaddr_ll sll;
        __memset(&sll, 0, sizeof(sll));
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = tlibc_htons(ETH_P_ARP);
        sll.sll_ifindex  = ifindex;
        sll.sll_halen    = ETH_ALEN;
        __memmove(sll.sll_addr, broadcast_mac, ETH_ALEN);

        int ret = __pkt_sendto(sock, &pkt, sizeof(pkt), &sll);
        if (ret >= 0) sent++;
    }
    printf("\n");

    if (sent_out) *sent_out = sent;

    /* 记录本机 */
    int result_count = 1;
    __memset(&results[0], 0, sizeof(results[0]));
    results[0].ip      = src_ip;
    __memmove(results[0].mac, src_mac, ETH_ALEN);
    results[0].is_self = 1;

    /* 监听 ARP 回复 */
    struct pollfd pfd;
    pfd.fd      = sock;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    unsigned char buf[sizeof(struct arp_pkt) + 64];

    while (result_count < max_results - 1) {
        pfd.revents = 0;
        int ret = __poll_wait(&pfd, 1, timeout_ms);
        if (ret < 0) {
            if (ret == -EINTR) continue;
            break;
        }
        if (ret == 0) break;

        struct sockaddr_ll sll;
        unsigned int sll_len = sizeof(sll);
        int n = __pkt_recvfrom(sock, buf, sizeof(buf), &sll, &sll_len);
        if (n < (int)sizeof(struct arp_pkt)) continue;

        struct arp_pkt *rp = (struct arp_pkt *)buf;
        unsigned short etype = tlibc_ntohs(rp->eth.type);
        if (etype != ETH_P_ARP) continue;

        unsigned short oper = tlibc_ntohs(rp->arp.oper);
        if (oper != ARPOP_REPLY) continue;

        unsigned int sender_ip = rp->arp.spa;
        if (sender_ip == src_ip) continue;

        int already = 0;
        for (int i = 0; i < result_count; i++) {
            if (results[i].ip == sender_ip) { already = 1; break; }
        }
        if (already) continue;

        __memset(&results[result_count], 0, sizeof(results[0]));
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
/*  ICMP TTL 探测 */
/* ================================================================== */

/* Internet 校验和 */
static unsigned short
icmp_checksum(void *data, int len)
{
    unsigned int sum = 0;
    unsigned short *ptr = (unsigned short *)data;
    while (len > 1) { sum += *ptr++; len -= 2; }
    if (len > 0) sum += *(unsigned char *)ptr;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)(~sum & 0xffff);
}

static void
icmp_probe(struct host_info *results, int count, int timeout_ms,
           const char *iface, unsigned int src_ip,
           const unsigned char *src_mac)
{
    int ifindex = get_ifindex(iface);
    if (ifindex < 0) return;

    int sock = socket(AF_PACKET, SOCK_RAW, tlibc_htons(ETH_P_IP));
    if (sock < 0) return;

    unsigned char broadcast_mac[ETH_ALEN];
    __memset(broadcast_mac, 0xff, ETH_ALEN);

    int sent = 0;

    /* ---- 发送 ICMP Echo Request ---- */
    for (int i = 0; i < count; i++) {
        if (results[i].is_self) continue;

        unsigned char pkt[14 + 20 + 8 + 40];
        __memset(pkt, 0, sizeof(pkt));
        int pos = 0;

        struct eth_hdr *eth = (struct eth_hdr *)(pkt + pos);
        __memmove(eth->dst, broadcast_mac, ETH_ALEN);
        __memmove(eth->src, src_mac, ETH_ALEN);
        eth->type = tlibc_htons(ETH_P_IP);
        pos += 14;

        struct ip_hdr *iph = (struct ip_hdr *)(pkt + pos);
        iph->ver_ihl     = 0x45;
        iph->tos         = 0;
        iph->total_len   = tlibc_htons(20 + 8 + 40);
        iph->id          = tlibc_htons(i + 1);
        iph->frag_off    = tlibc_htons(0x4000);
        iph->ttl         = 64;
        iph->protocol    = IPPROTO_ICMP;
        iph->checksum    = 0;
        iph->src_ip      = src_ip;
        iph->dst_ip      = results[i].ip;
        {
            unsigned int sum = 0;
            unsigned short *ptr = (unsigned short *)iph;
            for (int j = 0; j < 10; j++) sum += ptr[j];
            while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
            iph->checksum = (unsigned short)(~sum & 0xffff);
        }
        pos += 20;

        struct icmp_echo *icmp = (struct icmp_echo *)(pkt + pos);
        icmp->type     = ICMP_ECHO;
        icmp->code     = 0;
        icmp->id       = tlibc_htons(0x7d03);
        icmp->seq      = tlibc_htons(i);
        icmp->checksum = 0;
        pos += 8;

        for (int j = 0; j < 40; j++)
            pkt[pos++] = (unsigned char)(0x40 + j);

        icmp->checksum = icmp_checksum(pkt + 14 + 20, 8 + 40);

        struct sockaddr_ll sll;
        __memset(&sll, 0, sizeof(sll));
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = tlibc_htons(ETH_P_IP);
        sll.sll_ifindex  = ifindex;
        sll.sll_halen    = ETH_ALEN;
        __memmove(sll.sll_addr, broadcast_mac, ETH_ALEN);

        int ret = __pkt_sendto(sock, &pkt, sizeof(pkt), &sll);
        if (ret >= 0) sent++;
    }

    /* ---- 轮询接收 ---- */
    {
        unsigned char buf[512];

        struct pollfd pfd;
        pfd.fd      = sock;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        int max_replies = sent;
        while (max_replies > 0) {
            pfd.revents = 0;
            int ret = __poll_wait(&pfd, 1, timeout_ms);
            if (ret <= 0) break;

            struct sockaddr_ll sll;
            unsigned int sll_len = sizeof(sll);
            int n = __pkt_recvfrom(sock, buf, sizeof(buf), &sll, &sll_len);
            if (n < 14 + 20 + 8) continue;

            struct eth_hdr *re = (struct eth_hdr *)buf;
            if (tlibc_ntohs(re->type) != ETH_P_IP) continue;

            struct ip_hdr *riph = (struct ip_hdr *)(buf + 14);
            int ip_hdr_len = (riph->ver_ihl & 0x0f) * 4;
            if (14 + ip_hdr_len + 8 > n) continue;

            struct icmp_echo *ricmp = (struct icmp_echo *)(buf + 14 + ip_hdr_len);
            if (ricmp->type != ICMP_ECHOREPLY || ricmp->code != 0) continue;

            for (int j = 0; j < count; j++) {
                if (results[j].is_self) continue;
                if (results[j].ip == riph->src_ip && results[j].ttl == 0) {
                    results[j].ttl = riph->ttl;
                    max_replies--;
                    break;
                }
            }
        }
    }

    close(sock);
}

/* ================================================================== */
/*  主机排序 */
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
/*  结果显示 */
/* ================================================================== */

static void
print_results(struct host_info *results, int count,
              unsigned int network, unsigned int netmask,
              int sent, int timeout_ms, int do_port_scan)
{
    char net_str[16];
    ip_to_str(network, net_str, sizeof(net_str));

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
        char tag[120];
        char port_str[80];

        /* ---- 格式化端口信息 ---- */
        port_str[0] = '\0';
        if (do_port_scan && results[i].open_ports) {
            int pos = 0;
            pos += snprintf(port_str + pos, (int)sizeof(port_str) - pos, " ");
            int first = 1;
            for (unsigned int pi = 0; pi < NUM_PORTS; pi++) {
                if (results[i].open_ports & PORT_TABLE[pi].bit) {
                    const char *svc = PORT_TABLE[pi].svc;
                    int need = (first ? 0 : 1) + (int)strlen(svc) + 1;
                    if (pos + need < (int)sizeof(port_str)) {
                        if (!first) {
                            port_str[pos++] = ',';
                            port_str[pos] = '\0';
                        }
                        int svc_len = (int)strlen(svc);
                        __memmove(port_str + pos, svc, svc_len + 1);
                        pos += svc_len;
                        first = 0;
                    }
                }
            }
            if (first) port_str[0] = '\0';  /* no open ports after all */
        }

        if (results[i].is_self) {
            color = BRIGHT_GREEN_COLOR_PRINT;
            snprintf(tag, sizeof(tag), "This host%s", port_str);
        } else {
            const char *label = dev_class_label(results[i].dev_class);
            color = dev_class_color(results[i].dev_class);

            /* 构建 tag: {设备类型} {详情}{端口} */
            if (results[i].class_signal == 0) {
                /* OUI 精确匹配 — 显示厂商名，可附加 OS 信息 */
                if (results[i].os_info[0])
                    snprintf(tag, sizeof(tag), "%s %s (%s)%s",
                             label, results[i].vendor,
                             results[i].os_info, port_str);
                else
                    snprintf(tag, sizeof(tag), "%s %s%s",
                             label, results[i].vendor, port_str);
            } else {
                /* 启发式推断 — 显示 OS / 描述信息 */
                if (results[i].os_info[0])
                    snprintf(tag, sizeof(tag), "%s %s%s",
                             label, results[i].os_info, port_str);
                else
                    snprintf(tag, sizeof(tag), "%s%s",
                             label, port_str);
            }
        }

        printf("    %s%-15s (%s) [%s]%s\n",
               color, ip_s, mac_s, tag, COLOR_RESET);
    }

    printf("=> %s%d%s host%s found (sent %d probes, %dms timeout)%s\n",
           count > 1 ? BRIGHT_GREEN_COLOR_PRINT : YELLOW_COLOR_PRINT,
           count, COLOR_RESET,
           count > 1 ? "s" : "",
           sent, timeout_ms,
           do_port_scan ? ", port scan enabled" : "");
}

/* ================================================================== */
/*  用法信息 */
/* ================================================================== */

static void
print_usage(void)
{
    printf("Usage: ndiscover [options]\n");
    printf("\n");
    printf("Discovery options:\n");
    printf("  -i <interface>    Network interface (default: auto-detect)\n");
    printf("  -r <cidr>         Scan range (e.g. 192.168.1.0/24)\n");
    printf("  -t <timeout_ms>   ARP reply timeout in ms (default: %d)\n",
           DEFAULT_TIMEOUT_MS);
    printf("  --fast            Skip TCP port scan (ARP + ICMP only)\n");
    printf("\n");
    printf("TCP port scan (enabled by default):\n");
    printf("  Probes 7 common ports per host to identify devices:\n");
    printf("    port  22=SSH, 80=HTTP, 443=HTTPS, 554=RTSP(camera),\n");
    printf("    8080=HTTP-alt, 1883=MQTT(IoT), 9100=Printer\n");
    printf("  Use --fast to skip this for a quicker scan.\n");
    printf("\n");
    printf("OUI database options:\n");
    printf("  --oui-file <path> Load OUI file for vendor/type identification\n");
    printf("                    Built-in table covers TP-Link, Apple, Intel, etc.\n");
    printf("\n");
    printf("Other:\n");
    printf("  -h, --help        Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  ndiscover                     # Default full scan\n");
    printf("  ndiscover --fast              # Skip port scan\n");
    printf("  ndiscover --oui-file ./oui.txt\n");
    printf("\n");
    printf("Note: needs CAP_NET_RAW. Fix once: sudo setcap cap_net_raw+ep ~/tlibc/bin/ndiscover\n");
}

/* ================================================================== */
/*  主函数 */
/* ================================================================== */

int main(int argc, char *argv[])
{
    const char *iface      = NULL;
    int         timeout    = DEFAULT_TIMEOUT_MS;
    int         have_cidr  = 0;
    unsigned int cidr_ip   = 0;
    unsigned int cidr_mask = 0;
    int         do_port_scan = 1;      /* 默认开启端口探测 */
    const char *oui_file   = NULL;

    /* ---- 解析命令行参数 ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iface = argv[++i];
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            if (parse_cidr(argv[++i], &cidr_ip, &cidr_mask) < 0) {
                printf("Invalid CIDR: %s\n", argv[i]);
                return 1;
            }
            have_cidr = 1;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            timeout = parse_uint(argv[++i], 60000);
            if (timeout < 50) {
                printf("Invalid timeout: %s (50-60000)\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--scan-ports") == 0) {
            do_port_scan = 1;  /* 默认已开启，此为显式启用 */
        } else if (strcmp(argv[i], "--fast") == 0) {
            do_port_scan = 0;  /* 跳过端口探测，仅 ARP+ICMP */
        } else if (strcmp(argv[i], "--oui-file") == 0 && i + 1 < argc) {
            oui_file = argv[++i];
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    /* ---- 初始化 OUI ---- */
    if (oui_file) {
        oui_loaded = load_oui_file(oui_file, &oui_loaded_count);
        if (oui_loaded && oui_loaded_count > 0)
            printf("OUI: loaded %d entries from %s\n", oui_loaded_count, oui_file);
        else
            printf("OUI: cannot load %s, using built-in table (%d entries)\n",
                   oui_file, oui_builtin_count);
    } else {
        printf("OUI: using built-in table (%d entries)\n", oui_builtin_count);
    }
    printf("\n");

    /* ---- 自动检测接口 ---- */
    char auto_iface[IFNAMSIZ];
    if (!iface) {
        if (find_default_interface(auto_iface, sizeof(auto_iface)) < 0) {
            printf("Error: cannot detect network interface.\n");
            printf("Specify one with: ndiscover -i <interface>\n");
            return 1;
        }
        iface = auto_iface;
    }

    printf("Interface: %s\n", iface);

    /* ---- 获取本机 MAC ---- */
    unsigned char local_mac[ETH_ALEN];
    if (get_local_mac(iface, local_mac) < 0) {
        printf("Error: cannot read MAC for '%s'\n", iface);
        return 1;
    }
    char mac_str[18];
    mac_to_str(local_mac, mac_str, sizeof(mac_str));
    printf("Local MAC: %s\n", mac_str);

    /* ---- 获取本机 IP ---- */
    unsigned int local_ip;
    if (get_local_ip(iface, &local_ip) < 0) {
        printf("Error: cannot get IP for '%s'\n", iface);
        return 1;
    }
    char ip_str[16];
    ip_to_str(local_ip, ip_str, sizeof(ip_str));
    printf("Local IP:  %s\n", ip_str);

    /* ---- 确定扫描范围 ---- */
    unsigned int network, netmask;

    if (have_cidr) {
        netmask = cidr_mask;
        network = cidr_ip & netmask;
    } else {
        if (get_local_netmask(iface, &netmask) < 0) {
            printf("Error: cannot get netmask for '%s'\n", iface);
            return 1;
        }
        network = local_ip & netmask;
    }

    char net_str[16], mask_str[16];
    ip_to_str(network, net_str, sizeof(net_str));
    ip_to_str(netmask, mask_str, sizeof(mask_str));
    printf("Network:   %s (mask %s)\n\n", net_str, mask_str);

    /* ---- 读取默认网关（用于路由器识别）---- */
    unsigned int gateway_ip = get_default_gateway();
    if (gateway_ip) {
        char gw_str[16];
        ip_to_str(gateway_ip, gw_str, sizeof(gw_str));
        printf("Gateway:   %s\n\n", gw_str);
    }

    printf("Mode:      %s\n\n",
           do_port_scan ? "full (ARP+ICMP+port scan)"
                        : "fast (ARP+ICMP only)");

    /* ---- 执行 ARP 扫描 ---- */
    struct host_info results[MAX_RESULTS];
    int sent = 0;
    int n = arp_scan(iface, local_ip, local_mac,
                     network, netmask, timeout,
                     results, MAX_RESULTS, &sent);

    if (n < 0) return 1;

    /* ---- 执行 ICMP TTL 探测 ---- */
    printf("  Probing %d host%s for OS detection (ICMP ping)...\n",
           n - 1, n - 1 != 1 ? "s" : "");
    icmp_probe(results, n, 500, iface, local_ip, local_mac);

    /* ---- 执行 TCP 端口探测 ---- */
    if (do_port_scan && n > 1) {
        printf("  Port scanning %d host%s...\n",
               n - 1, n - 1 != 1 ? "s" : "");
        probe_ports(results, n, PORT_SCAN_TIMEOUT);
    }

    /* ---- 设备分类 ---- */
    for (int i = 0; i < n; i++)
        classify_device(&results[i], local_ip, gateway_ip);

    /* ---- 排序并显示 ---- */
    sort_hosts(results, n);
    print_results(results, n, network, netmask, sent, timeout, do_port_scan);

    return 0;
}
