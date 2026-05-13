#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h" //完整的结构体定义
#include "tlibc_types.h"

#define BUFFER_SIZE 1024

void* receive_thread(void* arg) {
    char buffer[1024];
    int sock = *(int*)arg;
    
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            PRINT_COLOR(RED_COLOR_PRINT ,"Server disconnected! should quit now\n");
            break;
        }
        buffer[n] = '\0';
        printf("[server] %s", buffer);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <IP address or domain> <port>\n", argv[0]);
        printf("Example: %s 120.26.254.205 8888\n", argv[0]);
        printf("Example: %s www.google.com 80\n", argv[0]);
        return 1;
    }
    
    char *server_addr = argv[1];
    int port = tlibc_strtoul(argv[2]);
    
    // 1. socket() - 创建TCP套接字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket() failed");
        return 1;
    }
    
    // 2. 解析服务器地址（支持域名和IP）
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = tlibc_htons(port);
    server.sin_addr.s_addr = tlibc_inet_addr(server_addr); //尝试将地址作为IP解析
    
    // 3. connect() - 连接到服务器
    printf("Connecting to %s:%d ...\n", server_addr, port);
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("connect() failed");
        close(sock);
        return 1;
    }
    printf("Connected successfully!\n");
    printf("Type your messages (Ctrl+C to exit, or type 'quit' to close)\n");
    printf("---------------------------------------------------------------\n");
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_thread, (void *)&sock);
    
    while(1){
        char buf[256];
        memset(buf, 0, 256);
        int read_count = read(0,buf,256); //读取一次输入
        if(read_count < 0)
        {
            printf("读取错误!重新读取\n");
            continue;
        }
        if(strcmp(buf, "quit\n") == 0)
        {
            printf("Quit! Closing connection...\n");
            break;
        }
        write(sock, buf, read_count); //发送输入到服务器
    }
    
    // 9. close() - 关闭套接字
    close(sock);
    printf("Connection closed.\n");
    return 0;
}