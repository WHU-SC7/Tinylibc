#include "core.h"
#include "tlibc_print.h"
#include "errno.h"
#include "tlibc_compat.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h" //完整的结构体定义
#include "tlibc_types.h"

#define PORT 8888
#define BUFFER_SIZE 1024

struct server_thread_arg {
    int client_fd;
    struct sockaddr_in client_addr;
};

void* server_thread_entry(void* arg) {
    struct server_thread_arg *thread_arg = (struct server_thread_arg*)arg;
    int client_fd = thread_arg->client_fd;
    struct sockaddr_in client_addr = thread_arg->client_addr;
    long bytes_received;
    char buffer[BUFFER_SIZE];

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';  // 添加字符串结束符
        printf("[%s:%d] %s",
               tlibc_inet_ntoa(client_addr.sin_addr),
               tlibc_ntohs(client_addr.sin_port),
               buffer);
        fprintf(client_fd, "Server receive your message: %s", buffer); // 回显给客户端
    }
    
    if (bytes_received == 0) {
        printf("Client %s:%d disconnected\n",
               tlibc_inet_ntoa(client_addr.sin_addr),
               tlibc_ntohs(client_addr.sin_port));
    } else if (bytes_received < 0) {
        printf("recv() error");
    }
    close(client_fd);
    printf("\n");
    munmap(thread_arg, sizeof(struct server_thread_arg));
        
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    
    // 支持命令行指定端口
    if (argc > 1) {
        port = tlibc_strtoul(argv[1]);
    }
    
    // 1. socket() - 创建TCP套接字
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("socket() failed");
        return 1;
    }
    
    // 2. setsockopt() - 设置端口复用（避免"Address already in use"）
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("setsockopt() failed");
        close(server_fd);
        return 1;
    }
    
    // 3. bind() - 绑定端口
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网络接口
    addr.sin_port = tlibc_htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("bind() failed");
        close(server_fd);
        return 1;
    }
    
    // 4. listen() - 开始监听
    if (listen(server_fd, 5) < 0) {
        printf("listen() failed");
        close(server_fd);
        return 1;
    }
    
    printf("Server listening on port %d\n", port);
    printf("Waiting for connections...\n\n");
    
    // 5. accept() 循环 - 接受客户端连接
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // 接受连接（阻塞）
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            printf("accept() failed");
            continue;
        }
        
        printf("New connection from %s:%d\n",
               tlibc_inet_ntoa(client_addr.sin_addr),
               tlibc_ntohs(client_addr.sin_port));
        
        pthread_t server_thread;
        struct server_thread_arg *thread_arg = (struct server_thread_arg*)mmap(0, sizeof(struct server_thread_arg), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        thread_arg->client_fd = client_fd;
        thread_arg->client_addr = client_addr;
        pthread_create(&server_thread, NULL, server_thread_entry, (void *)thread_arg);
    }
    
    // never reach here!
    close(server_fd);
    return 0;
}
