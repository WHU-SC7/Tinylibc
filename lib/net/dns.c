/* SPDX-License-Identifier: MIT
 *
 * dns.c — DNS 查询与解析（最小实现）
 *
 * 功能:
 *   - DNS 查询报文构建（A / AAAA / CNAME 等）
 *   - DNS 名称编解码（含压缩指针）
 *   - UDP 查询 / 接收
 *   - 响应解析（逐 section 遍历）
 *   - 高层 resolve：hostname → IPv4
 *
 * 命名: tlibc_dns_* 前缀，遵循项目内部 API 风格。
 */

#include "core.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "string.h"
#include "errno.h"

/* ================================================================== */
/*  内部辅助                                                           */
/* ================================================================== */

/* 跳过 wire format 域名（只移动位置，不输出）
 *   返回解析结束后的下一个偏移，-1 表示错误 */
static int
name_skip(const uint8_t *resp, int resp_len, int off)
{
    int jumped = 0;
    int first_ptr_off = off;

    while (off < resp_len) {
        uint8_t len = resp[off];
        if (len == 0) {
            if (!jumped) return off + 1;
            return first_ptr_off + 2;   /* 跳过第一个指针字节 */
        }
        if ((len & 0xC0) == 0xC0) {
            /* 压缩指针：高 2 位 11，低 14 位为偏移 */
            if (off + 1 >= resp_len) return -1;
            if (!jumped) {
                first_ptr_off = off;
                jumped = 1;
            }
            off = ((uint16_t)(len & 0x3F) << 8) | resp[off + 1];
            continue;
        }
        if (len > 63) return -1;        /* 每个 label 最多 63 字节 */
        if (off + 1 + len > resp_len) return -1;
        off += 1 + len;
    }
    return -1;
}

/* 跳过一条完整 RR：
 *   当前 off 必须在域名起始处
 *   返回 RR 末尾的下一个偏移，-1 表示错误 */
static int
rr_skip(const uint8_t *resp, int resp_len, int off)
{
    int pos = name_skip(resp, resp_len, off);
    if (pos < 0) return -1;
    if (pos + 10 > resp_len) return -1; /* TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2) */

    uint16_t rdlength = (uint16_t)resp[pos + 8] << 8 | resp[pos + 9];
    return pos + 10 + rdlength;
}

/* 计算指定 section 的起始偏移（0 基）：
 *   section 0=answer, 1=authority, 2=additional
 *   返回偏移，-1 表示错误 */
static int
section_start(const uint8_t *resp, int resp_len, int section)
{
    if (resp_len < DNS_HEADER_SIZE) return -1;

    struct dns_header hdr;
    hdr.id       = (uint16_t)resp[0]  << 8 | resp[1];
    hdr.flags    = (uint16_t)resp[2]  << 8 | resp[3];
    hdr.qdcount  = (uint16_t)resp[4]  << 8 | resp[5];
    hdr.ancount  = (uint16_t)resp[6]  << 8 | resp[7];
    hdr.nscount  = (uint16_t)resp[8]  << 8 | resp[9];
    hdr.arcount  = (uint16_t)resp[10] << 8 | resp[11];

    (void)hdr;

    /* 跳过 header + 所有 question */
    int pos = DNS_HEADER_SIZE;
    for (int i = 0; i < hdr.qdcount; i++) {
        pos = name_skip(resp, resp_len, pos);
        if (pos < 0) return -1;
        pos += 4;   /* QTYPE + QCLASS */
    }

    uint16_t counts[3] = { hdr.ancount, hdr.nscount, hdr.arcount };

    /* 跳过 section 0 … section-1 的所有 RR */
    for (int s = 0; s < section; s++) {
        for (int i = 0; i < counts[s]; i++) {
            pos = rr_skip(resp, resp_len, pos);
            if (pos < 0) return -1;
        }
    }

    return pos;
}

/* 从当前 off 解析 RR 固定字段（10 字节），填入输出参数 */
static int
rr_read_fixed(const uint8_t *resp, int resp_len, int off,
              uint16_t *type, uint16_t *class, uint32_t *ttl,
              uint16_t *rdlength)
{
    if (off + 10 > resp_len) return -1;
    if (type)    *type    = (uint16_t)resp[off]     << 8 | resp[off + 1];
    if (class)   *class   = (uint16_t)resp[off + 2] << 8 | resp[off + 3];
    if (ttl)     *ttl     = ((uint32_t)resp[off + 4] << 24) |
                             ((uint32_t)resp[off + 5] << 16) |
                             ((uint32_t)resp[off + 6] <<  8) |
                              (uint32_t)resp[off + 7];
    if (rdlength) *rdlength = (uint16_t)resp[off + 8] << 8 | resp[off + 9];
    return 0;
}

/* ================================================================== */
/*  DNS 名称编解码                                                      */
/* ================================================================== */

/* 编码域名（如 "www.example.com"）为 wire format
 *   返回编码后的长度（不含终结符 0x00），-1 表示缓冲区不足 */
static int
name_encode(uint8_t *buf, size_t buf_len, const char *name)
{
    size_t pos = 0;
    while (*name) {
        /* 找一个 label */
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - name);
        if (len > 63) return -1;
        if (pos + 1 + len + 1 > buf_len) return -1; /* +1 为终结 \0 */
        buf[pos++] = (uint8_t)len;
        for (int i = 0; i < len; i++)
            buf[pos++] = (uint8_t)*name++;
        if (*dot == '.') name++;
    }
    /* 终结 0x00 由调用者在返回位置写入 */
    return (int)pos;
}

/* 解码 wire format 域名为可打印字符串
 *   返回解码后下一个偏移，-1 表示错误 */
int
tlibc_dns_name_decode(const uint8_t *resp, int resp_len, int off,
                      char *out, int out_len)
{
    int out_pos = 0;
    int jumped = 0;
    int first_ptr_off = off;

    if (out && out_len > 0) out[0] = '\0';

    while (off < resp_len) {
        uint8_t len = resp[off];
        if (len == 0) {
            /* 名称结束 */
            if (out_pos == 0 && out && out_len > 1) {
                out[out_pos++] = '.';
            }
            if (out && out_pos < out_len) out[out_pos] = '\0';
            else if (out && out_len > 0)  out[out_len - 1] = '\0';
            if (!jumped) return off + 1;
            return first_ptr_off + 2;
        }
        if ((len & 0xC0) == 0xC0) {
            /* 压缩指针 */
            if (off + 1 >= resp_len) return -1;
            if (!jumped) {
                first_ptr_off = off;
                jumped = 1;
            }
            off = ((uint16_t)(len & 0x3F) << 8) | resp[off + 1];
            continue;
        }
        if (len > 63) return -1;
        if (off + 1 + len > resp_len) return -1;

        if (out_pos > 0 && out && out_pos < out_len - 1)
            out[out_pos++] = '.';
        for (int i = 0; i < len && out && out_pos < out_len - 1; i++)
            out[out_pos++] = resp[off + 1 + i];

        off += 1 + len;
    }
    return -1;
}

/* ================================================================== */
/*  查询报文构建                                                        */
/* ================================================================== */

int
tlibc_dns_build_query(uint8_t *buf, size_t buf_len,
                      const char *name, uint16_t qtype)
{
    if (buf_len < DNS_HEADER_SIZE + 5 + 4)    /* header + 最小空名称+1 + type+class */
        return -EINVAL;

    /* ---------- 随机 ID ---------- */
    uint16_t id;
    __getrandom(&id, sizeof(id), 0);
    if (id == 0) id = 0x1234;   /* fallback */

    /* ---------- Header ---------- */
    int pos = 0;
    buf[pos++] = (uint8_t)(id >> 8);
    buf[pos++] = (uint8_t)(id & 0xFF);
    buf[pos++] = 0x01;          /* flags 高: RD=1 */
    buf[pos++] = 0x00;          /* flags 低: 0 */
    buf[pos++] = 0x00;          /* QDCOUNT 高 */
    buf[pos++] = 0x01;          /* QDCOUNT 低 = 1 */
    buf[pos++] = 0x00;          /* ANCOUNT = 0 */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;          /* NSCOUNT = 0 */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;          /* ARCOUNT = 0 */
    buf[pos++] = 0x00;

    /* ---------- Question: QNAME ---------- */
    int nlen = name_encode(buf + pos, buf_len - pos, name);
    if (nlen < 0) return -EINVAL;
    pos += nlen;
    buf[pos++] = 0x00;          /* QNAME 终结 */

    /* ---------- QTYPE ---------- */
    if (pos + 4 > (int)buf_len) return -EINVAL;
    buf[pos++] = (uint8_t)(qtype >> 8);
    buf[pos++] = (uint8_t)(qtype & 0xFF);

    /* QCLASS = IN */
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    return pos;
}

/* ================================================================== */
/*  UDP 查询发送与接收                                                 */
/* ================================================================== */

int
tlibc_dns_query(uint32_t ns_ip, const uint8_t *query, int qlen,
                uint8_t *resp, int resp_cap)
{
    if (!query || qlen <= 0 || !resp || resp_cap <= 0)
        return -EINVAL;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return fd;

    struct sockaddr_in addr;
    __memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = tlibc_htons(DNS_PORT);
    addr.sin_addr.s_addr = ns_ip;

    /* 发送查询 */
    int ret = (int)sendto(fd, query, (size_t)qlen, 0,
                          (const struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        __close(fd);
        return ret;
    }

    /* 接收响应 */
    socklen_t addrlen = sizeof(addr);
    ret = (int)recvfrom(fd, resp, (size_t)resp_cap, 0,
                        (struct sockaddr *)&addr, &addrlen);
    __close(fd);
    return ret;
}

/* ================================================================== */
/*  响应解析 — 高层封装                                                */
/* ================================================================== */

void
tlibc_dns_parse_header(const uint8_t *resp, struct dns_header *hdr)
{
    if (!resp || !hdr) return;
    hdr->id      = (uint16_t)resp[0]  << 8 | resp[1];
    hdr->flags   = (uint16_t)resp[2]  << 8 | resp[3];
    hdr->qdcount = (uint16_t)resp[4]  << 8 | resp[5];
    hdr->ancount = (uint16_t)resp[6]  << 8 | resp[7];
    hdr->nscount = (uint16_t)resp[8]  << 8 | resp[9];
    hdr->arcount = (uint16_t)resp[10] << 8 | resp[11];
}

int
tlibc_dns_get_question(const uint8_t *resp, int resp_len, int n,
                       char *name, int name_len,
                       uint16_t *type, uint16_t *class)
{
    if (!resp || resp_len < DNS_HEADER_SIZE) return -EINVAL;

    struct dns_header hdr;
    tlibc_dns_parse_header(resp, &hdr);

    if (n < 0 || n >= hdr.qdcount) return -EINVAL;

    int pos = DNS_HEADER_SIZE;
    for (int i = 0; i < n; i++) {
        pos = name_skip(resp, resp_len, pos);
        if (pos < 0) return -1;
        pos += 4;
    }

    /* 解码 QNAME */
    pos = tlibc_dns_name_decode(resp, resp_len, pos, name, name_len);
    if (pos < 0) return pos;

    if (pos + 4 > resp_len) return -1;
    if (type)  *type  = (uint16_t)resp[pos] << 8 | resp[pos + 1];
    if (class) *class = (uint16_t)resp[pos + 2] << 8 | resp[pos + 3];
    return pos + 4;
}

int
tlibc_dns_get_record(const uint8_t *resp, int resp_len,
                     int section, int n,
                     char *name, int name_len,
                     uint16_t *type, uint16_t *class,
                     uint32_t *ttl,
                     const uint8_t **rdata, uint16_t *rdlen)
{
    if (!resp || resp_len < DNS_HEADER_SIZE || section < 0 || section > 2)
        return -EINVAL;

    struct dns_header hdr;
    tlibc_dns_parse_header(resp, &hdr);

    uint16_t counts[3] = { hdr.ancount, hdr.nscount, hdr.arcount };
    if (n < 0 || n >= counts[section]) return -EINVAL;

    int pos = section_start(resp, resp_len, section);
    if (pos < 0) return pos;

    /* 跳过前 n 条记录 */
    for (int i = 0; i < n; i++) {
        pos = rr_skip(resp, resp_len, pos);
        if (pos < 0) return -1;
    }

    /* 读取第 n 条记录 */
    int name_ret = name_skip(resp, resp_len, pos);
    if (name_ret < 0) return -1;

    if (name)
        tlibc_dns_name_decode(resp, resp_len, pos, name, name_len);

    int fixed_off = name_ret;
    if (rr_read_fixed(resp, resp_len, fixed_off, type, class, ttl, rdlen) < 0)
        return -1;

    if (rdata) *rdata = resp + fixed_off + 10;
    return fixed_off + 10 + (rdlen ? *rdlen : 0);
}

/* ================================================================== */
/*  /etc/resolv.conf 解析                                              */
/* ================================================================== */

int
tlibc_dns_get_nameserver(uint32_t *ns_ip)
{
    /* 默认 fallback */
    uint32_t fallback = tlibc_inet_addr("8.8.8.8");

    /* 尝试解析 /etc/resolv.conf */
    /* 使用静态缓冲区以兼容无 malloc 环境 */
    char buf[1024];
    int fd = __openat(AT_FDCWD, "/etc/resolv.conf", O_RDONLY, 0);
    if (fd < 0) {
        if (ns_ip) *ns_ip = fallback;
        return 0;
    }

    int n = (int)__read(fd, buf, sizeof(buf) - 1);
    __close(fd);

    if (n <= 0) {
        if (ns_ip) *ns_ip = fallback;
        return 0;
    }
    buf[n] = '\0';

    /* 逐行扫描 "nameserver" 关键字 */
    const char *prefix = "nameserver";
    int plen = 10;
    char *p = buf;

    while (*p) {
        /* 跳过行首空白 */
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, prefix, (size_t)plen) != 0) {
            /* 跳到下一行 */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        p += plen;
        while (*p == ' ' || *p == '\t') p++;
        /* 提取 IP 到临时字符串 */
        char ip_str[32];
        int ip_pos = 0;
        while (*p && *p != '\n' && *p != ' ' && *p != '\t' && *p != '#' && ip_pos < 31) {
            ip_str[ip_pos++] = *p++;
        }
        ip_str[ip_pos] = '\0';
        if (ip_pos > 0) {
            uint32_t ip = tlibc_inet_addr(ip_str);
            if (ip != 0xFFFFFFFF) {
                if (ns_ip) *ns_ip = ip;
                return 0;
            }
        }
        /* 无效 IP，继续下一行 */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (ns_ip) *ns_ip = fallback;
    return 0;
}

/* ================================================================== */
/*  高层 API：hostname → IPv4                                          */
/* ================================================================== */

/* CNAME 追踪深度限制 */
#define MAX_CNAME_CHAIN 8

int
tlibc_dns_resolve_ns(const char *hostname, uint32_t ns_ip, uint32_t *ip_out)
{
    if (!hostname || !ip_out) return -EINVAL;
    *ip_out = 0;

    /* 先尝试直接作为 IP 地址 */
    uint32_t maybe_ip = tlibc_inet_addr(hostname);
    if (maybe_ip != 0xFFFFFFFF) {
        *ip_out = maybe_ip;
        return 0;
    }

    uint8_t query[512];
    uint8_t resp[1024];

    int qlen = tlibc_dns_build_query(query, sizeof(query), hostname, DNS_TYPE_A);
    if (qlen < 0) return qlen;

    int rlen = tlibc_dns_query(ns_ip, query, qlen, resp, (int)sizeof(resp));
    if (rlen < 0) return rlen;

    /* 解析 header，检查 RCODE */
    struct dns_header hdr;
    tlibc_dns_parse_header(resp, &hdr);
    if ((hdr.flags & DNS_RCODE_MASK) != 0)
        return -ENOENT;                     /* NXDOMAIN 等 */
    if (hdr.ancount == 0)
        return -ENOENT;

    /* 追踪 CNAME 链 */
    char cname_buf[256];
    const char *current_name = hostname;
    int chain_depth = 0;

    while (chain_depth < MAX_CNAME_CHAIN) {
        int found_a = 0;
        uint16_t ancount = hdr.ancount;

        for (int i = 0; i < ancount; i++) {
            uint16_t rtype, rclass, rrdlen;
            uint32_t rttl;
            const uint8_t *rrdata;

            int ret = tlibc_dns_get_record(resp, rlen, 0, i,
                                           NULL, 0, &rtype, &rclass,
                                           &rttl, &rrdata, &rrdlen);
            if (ret < 0) continue;
            (void)rclass;
            (void)rttl;

            if (rtype == DNS_TYPE_A && rrdlen == 4) {
                /* DNS wire 格式是大端，直接转存为 uint32。
                   在小端 CPU 上，*(uint32_t*)rrdata 自然得到字节交换后的值，
                   这正是 s_addr 期望的格式。 */
                *ip_out = *(const uint32_t *)rrdata;
                found_a = 1;
                break;
            }

            if (rtype == DNS_TYPE_CNAME && chain_depth < MAX_CNAME_CHAIN - 1) {
                /* 从 RDATA 中解码 CNAME 目标 */
                char target[256];
                int off = tlibc_dns_name_decode(resp, rlen, (int)(rrdata - resp), target, sizeof(target));
                if (off > 0) {
                    /* 递归查询 CNAME 目标（注意：在实际实现中，
                     * 我们可能在同一响应中继续搜索，但为了简单，
                     * 这里将 target 设为 current_name 留给下次循环） */
                    int nlen = (int)strlen(target);
                    if (nlen >= (int)sizeof(cname_buf)) nlen = (int)sizeof(cname_buf) - 1;
                    memcpy(cname_buf, target, (size_t)nlen);
                    cname_buf[nlen] = '\0';
                }
            }
        }

        if (found_a) return 0;

        /* 没有 A 记录但有 CNAME：发起新一轮查询 */
        if (cname_buf[0]) {
            current_name = cname_buf;
            cname_buf[0] = '\0';
            qlen = tlibc_dns_build_query(query, sizeof(query), current_name, DNS_TYPE_A);
            if (qlen < 0) return qlen;
            rlen = tlibc_dns_query(ns_ip, query, qlen, resp, (int)sizeof(resp));
            if (rlen < 0) return rlen;
            tlibc_dns_parse_header(resp, &hdr);
            if ((hdr.flags & DNS_RCODE_MASK) != 0 || hdr.ancount == 0)
                return -ENOENT;
            chain_depth++;
        } else {
            break;
        }
    }

    return -ENOENT;
}

int
tlibc_dns_resolve(const char *hostname, uint32_t *ip_out)
{
    uint32_t ns_ip;
    int ret = tlibc_dns_get_nameserver(&ns_ip);
    if (ret < 0) return ret;
    return tlibc_dns_resolve_ns(hostname, ns_ip, ip_out);
}

/* ================================================================== */
/*  POSIX getaddrinfo / freeaddrinfo                                   */
/* ================================================================== */

int
getaddrinfo(const char *restrict host, const char *restrict serv,
            const struct addrinfo *restrict hint,
            struct addrinfo **restrict res)
{
    if (!host || !res) return EAI_NONAME;
    *res = NULL;

    int family   = 0;    /* 0 = AF_UNSPEC, 只支持 AF_INET */
    int socktype = 0;
    int protocol = 0;
    int flags    = 0;

    if (hint) {
        family   = hint->ai_family;
        socktype = hint->ai_socktype;
        protocol = hint->ai_protocol;
        flags    = hint->ai_flags;
        if (family != 0 && family != AF_INET)
            return EAI_FAMILY;
    }

    /* service = 端口号（暂不支持服务名） */
    uint16_t port = 0;
    if (serv) {
        unsigned long p = tlibc_strtoul((char *)serv);
        if (p == 0 && serv[0] != '0')
            return EAI_SERVICE;
        if (p > 65535)
            return EAI_SERVICE;
        port = (uint16_t)p;
    }

    /* 解析 host → IP */
    uint32_t ip;

    if (flags & AI_NUMERICHOST) {
        ip = tlibc_inet_addr(host);
        if (ip == 0xFFFFFFFF)
            return EAI_NONAME;
    } else {
        ip = tlibc_inet_addr(host);
        if (ip == 0xFFFFFFFF) {
            int ret = tlibc_dns_resolve(host, &ip);
            if (ret < 0)
                return EAI_NONAME;
        }
    }

    /* 分配结果 */
    struct addrinfo *ai = (struct addrinfo *)tlibc_malloc(sizeof(struct addrinfo));
    if (!ai) return EAI_MEMORY;
    __memset(ai, 0, sizeof(struct addrinfo));

    struct sockaddr_in *sa = (struct sockaddr_in *)tlibc_malloc(sizeof(struct sockaddr_in));
    if (!sa) {
        tlibc_free(ai);
        return EAI_MEMORY;
    }
    __memset(sa, 0, sizeof(struct sockaddr_in));
    sa->sin_family      = AF_INET;
    sa->sin_port        = tlibc_htons(port);
    sa->sin_addr.s_addr = ip;

    ai->ai_family   = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = protocol;
    ai->ai_addrlen  = sizeof(struct sockaddr_in);
    ai->ai_addr     = (struct sockaddr *)sa;

    if ((flags & AI_CANONNAME) && host) {
        size_t nlen = strlen(host);
        ai->ai_canonname = (char *)tlibc_malloc(nlen + 1);
        if (ai->ai_canonname) {
            memcpy(ai->ai_canonname, host, nlen);
            ai->ai_canonname[nlen] = '\0';
        }
    }

    *res = ai;
    return 0;
}

void
freeaddrinfo(struct addrinfo *res)
{
    while (res) {
        struct addrinfo *next = res->ai_next;
        if (res->ai_addr)
            tlibc_free(res->ai_addr);
        if (res->ai_canonname)
            tlibc_free(res->ai_canonname);
        tlibc_free(res);
        res = next;
    }
}
