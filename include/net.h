#ifndef __NET_H
#define __NET_H


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

int getaddrinfo(const char *restrict host, const char *restrict serv, const struct addrinfo *restrict hint, struct addrinfo **restrict res);

#endif