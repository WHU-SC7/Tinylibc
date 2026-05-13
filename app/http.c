#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h" //完整的结构体定义
#include "tlibc_types.h"



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