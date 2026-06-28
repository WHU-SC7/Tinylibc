/* SPDX-License-Identifier: MIT
 *
 * dnsquery — 简易 DNS 查询工具
 *
 * 对指定域名发起 DNS 查询并显示结果。
 * 支持 A / AAAA / MX / CNAME / NS / ANY 等记录类型。
 *
 * 用法:
 *   dnsquery <hostname>
 *   dnsquery -s <server> <hostname>
 *   dnsquery -t <type> <hostname>
 *   dnsquery -v <hostname>
 *
 * 示例:
 *   dnsquery example.com
 *   dnsquery -t MX gmail.com
 *   dnsquery -s 1.1.1.1 -v example.com
 *
 * 编译: tmake -b dnsquery
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
/*  类型名称映射                                                       */
/* ================================================================== */

static const char *
type_name(uint16_t type)
{
    switch (type) {
        case DNS_TYPE_A:     return "A";
        case DNS_TYPE_NS:    return "NS";
        case DNS_TYPE_CNAME: return "CNAME";
        case DNS_TYPE_SOA:   return "SOA";
        case DNS_TYPE_MX:    return "MX";
        case DNS_TYPE_AAAA:  return "AAAA";
        case DNS_TYPE_ANY:   return "ANY";
        default:             return "??";
    }
}

/* 解析记录类型字符串 → 数值 */
static int
parse_type(const char *s)
{
    if (!s) return DNS_TYPE_A;
         if (strcmp(s, "A")     == 0) return DNS_TYPE_A;
    else if (strcmp(s, "NS")    == 0) return DNS_TYPE_NS;
    else if (strcmp(s, "CNAME") == 0) return DNS_TYPE_CNAME;
    else if (strcmp(s, "SOA")   == 0) return DNS_TYPE_SOA;
    else if (strcmp(s, "MX")    == 0) return DNS_TYPE_MX;
    else if (strcmp(s, "AAAA")  == 0) return DNS_TYPE_AAAA;
    else if (strcmp(s, "ANY")   == 0) return DNS_TYPE_ANY;
    else return -1;
}

/* ================================================================== */
/*  时间测量                                                           */
/* ================================================================== */

static long
now_ms(void)
{
    struct timespec ts;
    __clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ================================================================== */
/*  IP 地址显示辅助                                                     */
/* ================================================================== */

/* 将网络字节序 IP 写入点分十进制字符串 */
static void
ip_str(uint32_t ip, char *buf, int buf_size)
{
    if (buf_size < 16) { buf[0] = '\0'; return; }
    unsigned char *b = (unsigned char *)&ip;
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        if (i > 0) buf[pos++] = '.';
        unsigned int v = (unsigned int)b[i];
        if (v >= 100) buf[pos++] = '0' + (v / 100);
        if (v >= 10)  buf[pos++] = '0' + ((v % 100) / 10);
        buf[pos++] = '0' + (v % 10);
    }
    buf[pos] = '\0';
}

/* 将 IPv6 16 字节写入冒号十六进制字符串（简化版，不压缩零） */
static void
ipv6_str(const uint8_t *addr, char *buf, int buf_size)
{
    int pos = 0;
    for (int i = 0; i < 16; i += 2) {
        if (i > 0 && pos < buf_size - 1)
            buf[pos++] = ':';
        if (pos + 4 >= buf_size) break;
        const char *hex = "0123456789abcdef";
        buf[pos++] = hex[(addr[i] >> 4) & 0x0F];
        buf[pos++] = hex[addr[i] & 0x0F];
        buf[pos++] = hex[(addr[i + 1] >> 4) & 0x0F];
        buf[pos++] = hex[addr[i + 1] & 0x0F];
    }
    if (pos < buf_size) buf[pos] = '\0';
    else buf[buf_size - 1] = '\0';
}

/* ================================================================== */
/*  输出辅助                                                           */
/* ================================================================== */

/* 用空格填充到指定宽度（精简版，不依赖 printf %-Ns） */
#define COL_NAME  32
#define COL_TTL    8
#define COL_CLASS  6
#define COL_TYPE   6

static void
print_padded(const char *s, int width)
{
    int n = 0;
    while (s[n]) n++;
    printf("%s", s);
    for (int i = n; i < width; i++) printf(" ");
}

/* 格式化并打印一条 RR 的 RDATA */
static void
print_rdata(uint16_t type, const uint8_t *rdata, uint16_t rdlen,
            const uint8_t *resp, int resp_len)
{
    switch (type) {
        case DNS_TYPE_A: {
            if (rdlen != 4) { printf("(bad len %d)", rdlen); break; }
            char ip[16];
            ip_str(*(const uint32_t *)rdata, ip, sizeof(ip));
            printf("%s", ip);
            break;
        }
        case DNS_TYPE_AAAA: {
            if (rdlen != 16) { printf("(bad len %d)", rdlen); break; }
            char ip6[48];
            ipv6_str(rdata, ip6, sizeof(ip6));
            printf("%s", ip6);
            break;
        }
        case DNS_TYPE_CNAME:
        case DNS_TYPE_NS: {
            char name[256];
            int off = tlibc_dns_name_decode(resp, resp_len,
                                            (int)(rdata - resp),
                                            name, sizeof(name));
            if (off > 0) printf("%s", name);
            else         printf("(decode error)");
            break;
        }
        case DNS_TYPE_MX: {
            if (rdlen < 2) { printf("(truncated)"); break; }
            uint16_t pref = (uint16_t)rdata[0] << 8 | rdata[1];
            char name[256];
            int off = tlibc_dns_name_decode(resp, resp_len,
                                            (int)(rdata - resp) + 2,
                                            name, sizeof(name));
            printf("%d ", pref);
            if (off > 0) printf("%s", name);
            else         printf("(decode error)");
            break;
        }
        case DNS_TYPE_SOA: {
            char mname[256], rname[256];
            int off = tlibc_dns_name_decode(resp, resp_len,
                                            (int)(rdata - resp),
                                            mname, sizeof(mname));
            if (off > 0) {
                off = tlibc_dns_name_decode(resp, resp_len, off,
                                            rname, sizeof(rname));
            }
            if (off > 0 && off + 20 <= (int)(rdata - resp) + rdlen) {
                uint32_t serial = (uint32_t)rdata[off - (int)(rdata - resp) + 0] << 24 |
                                  (uint32_t)rdata[off - (int)(rdata - resp) + 1] << 16 |
                                  (uint32_t)rdata[off - (int)(rdata - resp) + 2] <<  8 |
                                  (uint32_t)rdata[off - (int)(rdata - resp) + 3];
                printf("%s %s serial=%u", mname, rname, serial);
            } else {
                printf("(SOA parse error)");
            }
            break;
        }
        default:
            /* 未知类型，hex 显示 */
            printf("\\x");
            for (uint16_t i = 0; i < rdlen && i < 16; i++) {
                printf("%02x", rdata[i]);
            }
            if (rdlen > 16) printf("...");
            break;
    }
}

/* ================================================================== */
/*  显示 section                                                       */
/* ================================================================== */

static void
print_section(const char *label, const uint8_t *resp, int resp_len,
              int section, uint16_t count, int verbose)
{
    if (count == 0) {
        if (verbose)
            printf(";; %s: (empty)\n", label);
        return;
    }

    if (verbose)
        printf(";; %s:\n", label);

    for (uint16_t i = 0; i < count; i++) {
        char name[256];
        uint16_t rtype, rclass, rdlen;
        uint32_t ttl;
        const uint8_t *rdata;

        int ret = tlibc_dns_get_record(resp, resp_len, section, i,
                                       name, sizeof(name),
                                       &rtype, &rclass, &ttl,
                                       &rdata, &rdlen);
        if (ret < 0) {
            printf(";;   [record %d: parse error]\n", i);
            continue;
        }

        if (verbose) printf("  ");
        print_padded(name, COL_NAME);
        printf(" ");
        if (verbose) {
            printf("%ld ", (long)ttl);
        } else {
            /* 非详细模式只打印类型列，略过 TTL */
        }
        if (rclass == DNS_CLASS_IN) printf("IN  ");
        else                        printf("CLASS%d ", rclass);
        printf("%s  ", type_name(rtype));
        print_rdata(rtype, rdata, rdlen, resp, resp_len);
        printf("\n");
    }
}

/* ================================================================== */
/*  使用说明                                                           */
/* ================================================================== */

static void
usage(void)
{
    printf("Usage: dnsquery [options] <hostname>\n");
    printf("\n");
    printf("DNS lookup utility — minimal dig replacement\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s <server>    DNS server IP (default: /etc/resolv.conf or 8.8.8.8)\n");
    printf("  -t <type>      Record type: A, AAAA, MX, CNAME, NS, SOA, ANY (default: A)\n");
    printf("  -v             Verbose output (show header, stats, all sections)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  dnsquery example.com\n");
    printf("  dnsquery -t MX gmail.com\n");
    printf("  dnsquery -s 1.1.1.1 -v example.com\n");
}

/* ================================================================== */
/*  主函数                                                             */
/* ================================================================== */

int
main(int argc, char *argv[])
{
    const char *hostname   = NULL;
    uint32_t    ns_ip      = 0;
    int         qtype      = DNS_TYPE_A;
    int         verbose    = 0;
    int         has_ns     = 0;

    /* ---- 解析参数 ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
            continue;
        }
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                printf("Error: -s requires an IP argument\n");
                return 1;
            }
            uint32_t ip = tlibc_inet_addr(argv[++i]);
            if (ip == 0xFFFFFFFF) {
                printf("Error: invalid DNS server IP: %s\n", argv[i]);
                return 1;
            }
            ns_ip = ip;
            has_ns = 1;
            continue;
        }
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                printf("Error: -t requires a type argument\n");
                return 1;
            }
            qtype = parse_type(argv[++i]);
            if (qtype < 0) {
                printf("Error: unknown record type: %s\n", argv[i]);
                printf("Supported: A, AAAA, MX, CNAME, NS, SOA, ANY\n");
                return 1;
            }
            continue;
        }
        /* 位置参数 = hostname */
        if (!hostname) {
            hostname = argv[i];
        } else {
            printf("Error: unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!hostname) {
        usage();
        return 1;
    }

    /* ---- 确定 DNS 服务器 ---- */
    if (!has_ns) {
        int ret = tlibc_dns_get_nameserver(&ns_ip);
        if (ret < 0) {
            ns_ip = tlibc_inet_addr("8.8.8.8");
        }
    }

    char ns_str[16];
    ip_str(ns_ip, ns_str, sizeof(ns_str));

    if (verbose) {
        printf(";; Querying %s:%d\n", ns_str, DNS_PORT);
    }

    /* ---- 构造查询 ---- */
    uint8_t query[512];
    int qlen = tlibc_dns_build_query(query, sizeof(query), hostname, (uint16_t)qtype);
    if (qlen < 0) {
        printf("Error: failed to build DNS query (invalid hostname?)\n");
        return 1;
    }

    /* ---- 发送 & 接收 ---- */
    uint8_t resp[2048];
    long t1 = now_ms();
    int rlen = tlibc_dns_query(ns_ip, query, qlen, resp, (int)sizeof(resp));
    long t2 = now_ms();
    long elapsed = t2 - t1;

    if (rlen < 0) {
        printf("Error: query failed (errno %d)\n", -rlen);
        return 1;
    }

    if (rlen < DNS_HEADER_SIZE) {
        printf("Error: response too short (%d bytes)\n", rlen);
        return 1;
    }

    /* ---- 解析头部 ---- */
    struct dns_header hdr;
    tlibc_dns_parse_header(resp, &hdr);

    uint8_t rcode = hdr.flags & DNS_RCODE_MASK;

    if (verbose) {
        printf(";; HEADER: id=%u flags=0x%04x qd=%u an=%u ns=%u ar=%u\n",
               hdr.id, hdr.flags, hdr.qdcount,
               hdr.ancount, hdr.nscount, hdr.arcount);
        printf(";;   QR=%d OPCODE=%d AA=%d TC=%d RD=%d RA=%d RCODE=%d\n",
               (hdr.flags & DNS_FLAG_QR) ? 1 : 0,
               (hdr.flags >> 11) & 0x0F,
               (hdr.flags & DNS_FLAG_AA) ? 1 : 0,
               (hdr.flags & DNS_FLAG_TC) ? 1 : 0,
               (hdr.flags & DNS_FLAG_RD) ? 1 : 0,
               (hdr.flags & DNS_FLAG_RA) ? 1 : 0,
               rcode);
        printf("\n");
    }

    /* ---- RCODE 检查 ---- */
    if (rcode != 0) {
        const char *rcode_str = "Unknown";
        switch (rcode) {
            case 1: rcode_str = "Format error";   break;
            case 2: rcode_str = "Server failure"; break;
            case 3: rcode_str = "NXDOMAIN (name does not exist)"; break;
            case 4: rcode_str = "Not implemented"; break;
            case 5: rcode_str = "Refused";         break;
        }
        printf("%s\n", rcode_str);
        return 1;
    }

    /* ---- 问题区 ---- */
    if (verbose && hdr.qdcount > 0) {
        printf(";; QUESTION:\n");
        for (uint16_t i = 0; i < hdr.qdcount; i++) {
            char qname[256];
            uint16_t qtype_val, qclass_val;
            int ret = tlibc_dns_get_question(resp, rlen, i,
                                             qname, sizeof(qname),
                                             &qtype_val, &qclass_val);
            if (ret < 0) {
                printf(";;   [question %d: parse error]\n", i);
                continue;
            }
            printf("; ");
            print_padded(qname, COL_NAME);
            printf(" ");
            printf("IN  ");
            printf("%s\n", type_name(qtype_val));
        }
        printf("\n");
    }

    /* ---- 回答区 ---- */
    if (hdr.ancount > 0) {
        print_section("ANSWER", resp, rlen, 0, hdr.ancount, verbose);
        if (verbose) printf("\n");
    } else if (verbose) {
        printf(";; ANSWER: (empty)\n\n");
    }

    /* ---- 授权区 ---- */
    if (verbose && hdr.nscount > 0) {
        print_section("AUTHORITY", resp, rlen, 1, hdr.nscount, verbose);
        printf("\n");
    }

    /* ---- 附加区 ---- */
    if (verbose && hdr.arcount > 0) {
        print_section("ADDITIONAL", resp, rlen, 2, hdr.arcount, verbose);
        printf("\n");
    }

    /* ---- 统计 ---- */
    if (verbose) {
        printf(";; Query time: %ld ms\n", elapsed);
        printf(";; SERVER: %s#%d\n", ns_str, DNS_PORT);
        printf(";; MSG SIZE: rcvd=%d\n", rlen);
    }

    return 0;
}
