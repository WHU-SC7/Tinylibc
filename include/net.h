#ifndef __NET_H
#define __NET_H

#include "socket.h"

typedef unsigned int socklen_t;
/* 独立定义 struct sockaddr*/
struct sockaddr {
    unsigned short sa_family;   /* 地址族，例如 AF_INET = 2 */
    char           sa_data[14]; /* 协议地址数据（14 字节） */
};

struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr,socklen_t *addrlen);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);

unsigned short tlibc_htons(unsigned short host_val);
char* tlibc_inet_ntoa(struct in_addr in);
uint16_t tlibc_ntohs(uint16_t netshort);
unsigned int tlibc_inet_addr(const char *cp);
int getaddrinfo(const char *restrict host, const char *restrict serv, const struct addrinfo *restrict hint, struct addrinfo **restrict res);
void freeaddrinfo(struct addrinfo *res);

/* getaddrinfo hints.ai_flags */
#define AI_PASSIVE      0x0001
#define AI_CANONNAME    0x0002
#define AI_NUMERICHOST  0x0004

/* getaddrinfo 返回值（EAI_*，负值） */
#define EAI_BADFLAGS    -1
#define EAI_NONAME      -2
#define EAI_AGAIN       -3
#define EAI_FAIL        -4
#define EAI_FAMILY      -5
#define EAI_MEMORY      -6
#define EAI_SERVICE     -7
#define EAI_SOCKTYPE    -8
#define EAI_NODATA      -9

#define htons(host_val) tlibc_htons(host_val)
#define ntohs(netshort) tlibc_ntohs(netshort)
#define inet_ntoa(in) tlibc_inet_ntoa(in)
#define inet_addr(cp) tlibc_inet_addr(cp)

/* ================================================================== */
/*  DNS 基础定义                                                       */
/* ================================================================== */

#define DNS_PORT         53

/* DNS 记录类型 */
#define DNS_TYPE_A        1
#define DNS_TYPE_NS       2
#define DNS_TYPE_CNAME    5
#define DNS_TYPE_SOA      6
#define DNS_TYPE_MX      15
#define DNS_TYPE_AAAA    28
#define DNS_TYPE_ANY    255

/* DNS 类 */
#define DNS_CLASS_IN      1

/* DNS 头部 flags 字段位掩码 */
#define DNS_FLAG_QR     (1 << 15)
#define DNS_FLAG_AA     (1 << 10)
#define DNS_FLAG_TC     (1 << 9)
#define DNS_FLAG_RD     (1 << 8)
#define DNS_FLAG_RA     (1 << 7)
#define DNS_RCODE_MASK   0x0F

/* DNS header — 12 字节 */
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

#define DNS_HEADER_SIZE 12

/* DNS 查询问题（question 部分之后紧接的 4 字节） */
#define DNS_QTYPE_OFF   0   /* 相对于 QNAME 末尾的偏移 */
#define DNS_QCLASS_OFF  2

/* ================================================================== */
/*  DNS 库函数声明                                                      */
/* ================================================================== */

/* 构建 DNS 查询报文到 buf，返回报文长度（<0 表示错误） */
int tlibc_dns_build_query(uint8_t *buf, size_t buf_len,
                          const char *name, uint16_t qtype);

/* 向 nameserver 发送查询并接收响应
 *   ns_ip — nameserver IP（网络字节序的 uint32_t）
 *   返回接收到的响应长度，<0 表示错误 */
int tlibc_dns_query(uint32_t ns_ip, const uint8_t *query, int qlen,
                    uint8_t *resp, int resp_cap);

/* 解码 DNS 压缩名称（从 resp 的 off 偏移量开始读）
 *   返回解码后的下一个位置（可继续解析后续字段），<0 表示错误 */
int tlibc_dns_name_decode(const uint8_t *resp, int resp_len, int off,
                          char *out, int out_len);

/* 解析 DNS 响应头部 */
void tlibc_dns_parse_header(const uint8_t *resp, struct dns_header *hdr);

/* 获取第 n 个问题（0-based）的 name / type / class
 *   返回下一个位置（跳过问题后的偏移），<0 表示错误 */
int tlibc_dns_get_question(const uint8_t *resp, int resp_len, int n,
                           char *name, int name_len,
                           uint16_t *type, uint16_t *class);

/* 获取第 n 个资源记录（section：0=answer, 1=authority, 2=additional）
 *   name/type/class/ttl/rdata/rdlen 可为 NULL（跳过对应字段）
 *   返回下一个位置，<0 表示错误 */
int tlibc_dns_get_record(const uint8_t *resp, int resp_len,
                         int section, int n,
                         char *name, int name_len,
                         uint16_t *type, uint16_t *class,
                         uint32_t *ttl,
                         const uint8_t **rdata, uint16_t *rdlen);

/* 简易解析：将 hostname 解析为 IPv4 地址
 *   使用 /etc/resolv.conf 中的第一个 nameserver（或 8.8.8.8）
 *   返回 0 成功，<0 错误 */
int tlibc_dns_resolve(const char *hostname, uint32_t *ip_out);

/* 与 tlibc_dns_resolve 相同但指定 nameserver IP */
int tlibc_dns_resolve_ns(const char *hostname, uint32_t ns_ip, uint32_t *ip_out);

/* 获取默认 nameserver IP（网络字节序），返回 0 成功 */
int tlibc_dns_get_nameserver(uint32_t *ns_ip);

#endif