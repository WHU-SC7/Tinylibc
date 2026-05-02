#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h" //完整的结构体定义
#include "tlibc_types.h"


//x86_64的小端字节序转换为网络的大端
//小端，低位字节在低地址
//1    0    地址
//高位 低位  值
unsigned short tlibc_htons(unsigned short host_val){
    return (host_val << 8) | (host_val >> 8);
}

//莫名其妙的问题,等待审阅
unsigned int tlibc_inet_addr(const char *cp){
        unsigned int bytes[4] = {0};
    int part = 0;
    unsigned int val = 0;
    int digit_seen = 0;

    if (cp == NULL) return 0xFFFFFFFF;

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

int main() {
    // 1. 创建 socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    // 2. 构造服务器地址
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    // addr.sin_port   = htons(80);
    addr.sin_port   = tlibc_htons(80);
    //addr.sin_addr.s_addr = inet_addr("110.242.74.102"); //baidu.com
    addr.sin_addr.s_addr = tlibc_inet_addr("172.66.147.243"); //example.com
                                        //  FC 93 42 AC 大端 40
                                        //  AC 42 93 FC 小端 24
    // 3. 建立连接
    connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    // 4. 发送 HTTP 请求
    char *req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    __write(sock, req, strlen(req));

    // 5. 接收回复
    char buf[4096];
    int n = __read(sock, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    printf("%s\n", buf);

    // 6. 关闭
    __close(sock);
    return 0;
}