#include "net.h"
#include "syscall.h"
#include "syscall_num.h"
#include "tlibc_types.h"
#include "socket.h"

int socket(int domain, int type, int protocol){
    return syscall(SYS_socket, domain, type, protocol);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    return syscall(SYS_connect, sockfd, addr, addrlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return syscall(SYS_setsockopt, sockfd, level, optname, optval, optlen);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return syscall(SYS_bind, sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog)
{
    return syscall(SYS_listen, sockfd, backlog);
}

int accept(int sockfd, struct sockaddr *addr,socklen_t *addrlen)
{
    return syscall(SYS_accept, sockfd, addr, addrlen);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return syscall(SYS_recvfrom, sockfd, buf, len, flags, 0, 0);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return syscall(SYS_sendto, sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    return syscall(SYS_recvfrom, sockfd, buf, len, flags, src_addr, addrlen);
}

int shutdown(int sockfd, int how)
{
    return syscall(SYS_shutdown, sockfd, how);
}

/* 库函数 */

//x86_64的小端字节序转换为网络的大端
//小端，低位字节在低地址
//1    0    地址
//高位 低位  值
unsigned short tlibc_htons(unsigned short host_val){
    return (host_val << 8) | (host_val >> 8);
}

/* 将网络字节序的IP地址转换为点分十进制字符串 */
char* tlibc_inet_ntoa(struct in_addr in) {
    static char buffer[16];  /* 足够存储 "255.255.255.255" 及结尾符 */
    uint32_t addr = in.s_addr;
    uint8_t *bytes = (uint8_t*)&addr;
    uint8_t a, b, c, d;
    
    /* 网络字节序是大端，在x86_64小端下需要反转字节顺序 */
    /* 如果 addr 已经是主机字节序，则直接使用；这里按标准 inet_ntoa 接受网络字节序 */
    a = bytes[3];  /* 最高地址字节对应 IP 的第一个字节（大端） */
    b = bytes[2];
    c = bytes[1];
    d = bytes[0];
    
    /* 手动格式化字符串，不使用任何库函数 */
    char *p = buffer;
    
    /* 转换第一个数字 (a) */
    if (a >= 100) *p++ = '0' + (a / 100);
    if (a >= 10)  *p++ = '0' + ((a % 100) / 10);
    *p++ = '0' + (a % 10);
    *p++ = '.';
    
    /* 转换第二个数字 (b) */
    if (b >= 100) *p++ = '0' + (b / 100);
    if (b >= 10)  *p++ = '0' + ((b % 100) / 10);
    *p++ = '0' + (b % 10);
    *p++ = '.';
    
    /* 转换第三个数字 (c) */
    if (c >= 100) *p++ = '0' + (c / 100);
    if (c >= 10)  *p++ = '0' + ((c % 100) / 10);
    *p++ = '0' + (c % 10);
    *p++ = '.';
    
    /* 转换第四个数字 (d) */
    if (d >= 100) *p++ = '0' + (d / 100);
    if (d >= 10)  *p++ = '0' + ((d % 100) / 10);
    *p++ = '0' + (d % 10);
    *p = '\0';
    
    return buffer;
}

uint16_t tlibc_ntohs(uint16_t netshort) {
    /* x86_64 is little-endian, network is big-endian: swap bytes */
    return (netshort << 8) | (netshort >> 8);
}

// TODO: verify correctness, known issues
unsigned int tlibc_inet_addr(const char *cp){
        unsigned int bytes[4] = {0};
    int part = 0;
    unsigned int val = 0;
    int digit_seen = 0;

    if (cp == ((void *)0)) return 0xFFFFFFFF;

    while (*cp) {
        if (*cp >= '0' && *cp <= '9') {
            val = val * 10 + (*cp - '0');
            digit_seen = 1;
            if (val > 255) return 0xFFFFFFFF;
        } else if (*cp == '.') {
            if (!digit_seen || part >= 3) return 0xFFFFFFFF;
            bytes[part++] = val;
            val = 0;
            digit_seen = 0;
        } else {
            return 0xFFFFFFFF;
        }
        cp++;
    }
    if (!digit_seen || part != 3) return 0xFFFFFFFF;
    bytes[part] = val;

    /* 在小端机器上，内存布局为 bytes[0] (172) bytes[1] (66) bytes[2] (147) bytes[3] (243)
       将其直接解释为 32 位整数（小端）得到 243<<24 + 147<<16 + 66<<8 + 172 = 0xF39342AC = 4086514348 */
    return (bytes[3] << 24) | (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
}