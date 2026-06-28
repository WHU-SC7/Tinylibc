#include "tlibc_everything.h"

/* DNS 响应中最多解析的 A 记录数 */
#define MAX_IPS 16

static void print_usage(const char *prog)
{
    printf("Usage: %s <host> [<path>] [<port>] [<Host-header>]\n", prog);
    printf("Examples:\n");
    printf("  %s example.com\n", prog);
    printf("  %s example.com /index.html\n", prog);
    printf("  %s example.com / 8080\n", prog);
    printf("  %s 93.184.216.34 / 80 example.com\n", prog);
    printf("                               ^^^^ IP 直连时用此指定 Host 头\n");
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    char *host      = argv[1];
    char *path      = argc > 2 ? argv[2] : "/";
    int   port      = argc > 3 ? (int)tlibc_strtoul(argv[3]) : 80;
    char *host_hdr  = argc > 4 ? argv[4] : NULL;

    /* 判断 host 是否为 IP（而不是域名） */
    uint32_t raw_ip = tlibc_inet_addr(host);
    int host_is_ip  = (raw_ip != 0xFFFFFFFF);

    /* 确定 Host 头 */
    char *header_host;
    if (host_hdr) {
        header_host = host_hdr;
    } else if (host_is_ip) {
        header_host = host;
        printf("Warning: '%s' is an IP address. Virtual-hosted sites (e.g. Cloudflare)\n"
               "  need a proper Host header. Pass the domain as 4th argument:\n"
               "    %s %s <path> <port> <domain>\n",
               host, argv[0], host);
    } else {
        header_host = host;
    }

    /* ---- 解析目标 IP 列表 ---- */
    uint32_t ips[MAX_IPS];
    int ip_count = 0;

    if (host_is_ip) {
        ips[0] = raw_ip;
        ip_count = 1;
    } else {
        /* 手动 DNS 查询，获取所有 A 记录 */
        uint32_t ns_ip;
        if (tlibc_dns_get_nameserver(&ns_ip) < 0)
            ns_ip = tlibc_inet_addr("8.8.8.8");

        uint8_t query[512];
        int qlen = tlibc_dns_build_query(query, sizeof(query), host, DNS_TYPE_A);
        if (qlen < 0) {
            printf("Failed to build DNS query for %s\n", host);
            return 1;
        }

        uint8_t resp[2048];
        int rlen = tlibc_dns_query(ns_ip, query, qlen, resp, (int)sizeof(resp));
        if (rlen < DNS_HEADER_SIZE) {
            printf("Failed to resolve %s (DNS error)\n", host);
            return 1;
        }

        struct dns_header hdr;
        tlibc_dns_parse_header(resp, &hdr);
        if ((hdr.flags & DNS_RCODE_MASK) != 0 || hdr.ancount == 0) {
            printf("Failed to resolve %s (NXDOMAIN)\n", host);
            return 1;
        }

        /* 遍历所有 answer records，收集 A 记录 */
        for (int i = 0; i < hdr.ancount && ip_count < MAX_IPS; i++) {
            uint16_t rtype, rrdlen;
            uint32_t rttl;
            const uint8_t *rrdata;
            int ret = tlibc_dns_get_record(resp, rlen, 0, i,
                                           NULL, 0, &rtype, NULL,
                                           &rttl, &rrdata, &rrdlen);
            if (ret < 0) continue;
            if (rtype == DNS_TYPE_A && rrdlen == 4) {
                /* DNS wire 是大端，直接转存 uint32 自然得到 s_addr 所需格式 */
                ips[ip_count++] = *(const uint32_t *)rrdata;
            }
        }

        if (ip_count == 0) {
            printf("Failed to resolve %s (no A records)\n", host);
            return 1;
        }
    }

    /* ---- 创建 socket 并逐个 IP 尝试连接 ---- */
    int sock = -1;
    for (int i = 0; i < ip_count; i++) {
        char ip_str[16];
        unsigned char *b = (unsigned char *)&ips[i];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);

        printf("Trying %s:%d ...\n", ip_str, port);

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            printf("  socket() failed\n");
            continue;
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port   = tlibc_htons(port);
        addr.sin_addr.s_addr = ips[i];

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            printf("  connected.\n");
            break;  /* 连接成功 */
        }

        printf("  connect() failed (unreachable?)\n");
        __close(sock);
        sock = -1;
    }

    if (sock < 0) {
        printf("Failed to connect to %s:%d (all IPs tried)\n", host, port);
        return 1;
    }

    /* ---- 发送 HTTP 请求 ---- */
    char req[4096];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", path, header_host);
    printf("> GET %s HTTP/1.1\n> Host: %s\n", path, header_host);
    __write(sock, req, n);

    /* ---- 接收回复 ---- */
    char buf[4096];
    n = __read(sock, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("\n%s\n", buf);
    } else {
        printf("No response received.\n");
    }

    __close(sock);
    return 0;
}
