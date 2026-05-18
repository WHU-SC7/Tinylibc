#include "tlibc_everything.h"

#define BUFFER_SIZE 1024

void* receive_thread(void* arg) {
    char buffer[1024];
    int sock = *(int*)arg;
    
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        buffer[n] = '\0';  // 添加字符串结束符
        if(buffer[0] == '!'){ //服务器控制字
            if(strcmp(buffer, "!sendfile") == 0){
                memset(buffer, 0, sizeof(buffer));
                write(sock, "!readyforfilename", 17); //告诉服务器准备好接收文件名了
                int name_len =recv(sock, buffer, sizeof(buffer) - 1, 0); //接收文件名
                char file_name[256];
                memset(file_name, 0, sizeof(file_name));
                strncpy(file_name, buffer, name_len);
                memset(buffer, 0, sizeof(buffer));

                write(sock, "!readyforfilelen", 16); //告诉服务器准备好接收文件长度了
                recv(sock, buffer, sizeof(buffer) - 1, 0); //接收文件长度
                int file_len = tlibc_strtoul(buffer);
                char *file_buf = (char *)mmap(0, file_len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
                memset(file_buf, 0, file_len);

                write(sock, "!readyforfilecontent", 20); //告诉服务器准备好接收文件内容了
                recv(sock, file_buf, file_len, 0); //接收文件内容

                int fd = openat(AT_FDCWD, file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                write(fd, file_buf, file_len);
                close(fd);
                // printf("文件内容:\n%s\n", file_buf);
                munmap(file_buf, file_len);
                PRINT_COLOR(GREEN_COLOR_PRINT, "从服务器接收文件'%s'，大小: %d bytes，保存在当前目录下!\n", file_name, file_len);
                continue;
            }
        }
        if (n <= 0) {
            PRINT_COLOR(RED_COLOR_PRINT ,"Server disconnected! should quit now\n");
            break;
        }
        buffer[n] = '\0';
        PRINT_COLOR(BLUE_COLOR_PRINT ,"%s", buffer);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    // char *server_addr = "120.26.254.205";
    char *server_addr = "127.0.0.1";
    int port = 26517;
    if(argc ==1){

    }
    else if(argc == 2)
    {
        server_addr = argv[1];
    }
    else
    {
        server_addr = argv[1];
        port = tlibc_strtoul(argv[2]);
    }
    
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