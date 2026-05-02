#include "net.h"
#include "syscall.h"
#include "syscall_num.h"

int socket(int domain, int type, int protocol){
    return syscall(SYS_socket, domain, type, protocol);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    return syscall(SYS_connect, sockfd, addr, addrlen);
}
