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

unsigned short tlibc_htons(unsigned short host_val);
char* tlibc_inet_ntoa(struct in_addr in);
uint16_t tlibc_ntohs(uint16_t netshort);
unsigned int tlibc_inet_addr(const char *cp);
int getaddrinfo(const char *restrict host, const char *restrict serv, const struct addrinfo *restrict hint, struct addrinfo **restrict res);

#define htons(host_val) tlibc_htons(host_val)
#define ntohs(netshort) tlibc_ntohs(netshort)
#define inet_ntoa(in) tlibc_inet_ntoa(in)
#define inet_addr(cp) tlibc_inet_addr(cp)

#endif