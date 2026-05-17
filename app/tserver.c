#include "tlibc_everything.h"

#define PORT 26517
#define BUFFER_SIZE 1024

int server_log_fd = -1;
#define SERVER_LOG(fmt, ...) do { \
    if(server_log_fd >= 0) { \
        fdprintf(server_log_fd, fmt, ##__VA_ARGS__); \
        printf(fmt, ##__VA_ARGS__); \
    } \
} while(0)

#define SERVER_LOG_COLOR(color, fmt, ...) do { \
    if(server_log_fd >= 0) { \
        fdprintf(server_log_fd, fmt, ##__VA_ARGS__); \
        printf(color fmt COLOR_RESET, ##__VA_ARGS__); \
    } \
} while(0)

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
    char server_dir[256];
    tlibc_get_user_dir(server_dir, 256);
    strncat(server_dir, "/tserver", 255 - strlen(server_dir));
    int srv_fd = openat(AT_FDCWD, server_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if(srv_fd < 0) {
        mkdirat(AT_FDCWD, server_dir, 0777);
    }
    #define LS_BUF_SIZE 1024*1024
    char *ls_buf = (char *)mmap(0, LS_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    fdprintf(client_fd, "Welcome to Tinylibc TCP Server! \n");

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';  // 添加字符串结束符
        if(buffer[0] == '!'){ //命令以!开头
            if(strcmp(buffer, "!help\n") == 0){
                fdprintf(client_fd, "Available commands:\n");
                fdprintf(client_fd, "  !help - Show this help message\n");
                fdprintf(client_fd, "  !time - Show server time\n");
                fdprintf(client_fd, "  !exit - Close the connection\n");
                fdprintf(client_fd, "  !ls - List files in server directory\n");
                fdprintf(client_fd, "  或输入不以'!'开头的字符串以下载文件)\n");
            }
            else if(strcmp(buffer, "!time\n") == 0){
                struct timespec tp;
                __clock_gettime(CLOCK_REALTIME, &tp);
                fdprintf(client_fd, "Server time: %ld.%ld seconds since the Epoch\n", tp.tv_sec, tp.tv_nsec);
            }
            else if(strcmp(buffer, "!exit\n") == 0){
                fdprintf(client_fd, "Goodbye!\n");
                break;
            }
            else if(strcmp(buffer, "!ls\n") == 0){
                //列出tserver目录下的文件
                memset(ls_buf, 0, LS_BUF_SIZE);
                lseek(srv_fd, 0, SEEK_SET); //getdents64会改变目录fd的偏移量，需要重置
                int ret = getdents64(srv_fd, (struct linux_dirent64 *)ls_buf, LS_BUF_SIZE);
                if(ret < 0){
                    fdprintf(client_fd, "getdents64失败, 错误码: %d\n", ret);
                    continue;
                }
                struct linux_dirent64 *data = (struct linux_dirent64 *)ls_buf;
                while(data->d_off != 0){
                    fdprintf(client_fd, "%s ", data->d_name);
                    data = (struct linux_dirent64 *)((char *)data + data->d_reclen); //< 遍历
                }
                fdprintf(client_fd, "\n");
            }
            else{
                fdprintf(client_fd, "Unknown command: %s", buffer);
            }
        }
        else{ //现在默认是请求文件
            buffer[bytes_received-1] = 0; //把换行符改成0，构成字符串
            SERVER_LOG("Client %s:%d requested file: %s\n",
                   tlibc_inet_ntoa(client_addr.sin_addr),
                   tlibc_ntohs(client_addr.sin_port),
                   buffer);
            memset(ls_buf, 0, LS_BUF_SIZE);
            lseek(srv_fd, 0, SEEK_SET);
            int ret = getdents64(srv_fd, (struct linux_dirent64 *)ls_buf, LS_BUF_SIZE);
                if(ret < 0){
                    fdprintf(client_fd, "getdents64失败, 错误码: %d\n", ret);
                    continue;
                }
            struct linux_dirent64 *data = (struct linux_dirent64 *)ls_buf;
            while(data->d_off != 0){
                if(strcmp(data->d_name, buffer) == 0){
                    // fdprintf(client_fd, "File %s exists in server directory.\n", buffer);
                    //发送文件内容
                    char file_path[512];
                    strncpy(file_path, server_dir, 511);
                    strncat(file_path, "/", 511 - strlen(file_path));
                    strncat(file_path, buffer, 511 - strlen(file_path));
                    int file_fd = openat(AT_FDCWD, file_path, O_RDONLY, 0644);
                    if(file_fd < 0){
                        fdprintf(client_fd, "无法打开文件%s, 错误码: %d\n", buffer, file_fd);
                        break;
                    }
                    int file_len = tlibc_get_file_len(file_path);
                    char *file_buf = (char *)mmap(0, file_len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
                    read(file_fd, file_buf, file_len);

                    char ready_buf[32]; //用来接收client的回应
                    fdprintf(client_fd, "!sendfile");
                    recv(client_fd, ready_buf, sizeof(ready_buf) - 1, 0); //等待client准备好接收文件名
                    SERVER_LOG_COLOR(GREEN_COLOR_PRINT, "client said: %s\n", ready_buf);

                    fdprintf(client_fd, "%s", buffer);// 告知client文件名
                    recv(client_fd, ready_buf, sizeof(ready_buf) - 1, 0); //等待client准备好接收文件长度
                    SERVER_LOG_COLOR(GREEN_COLOR_PRINT, "client said: %s\n", ready_buf);

                    fdprintf(client_fd, "%d", file_len);// 告知client文件长度
                    recv(client_fd, ready_buf, sizeof(ready_buf) - 1, 0); //等待client准备好接收文件内容
                    SERVER_LOG_COLOR(GREEN_COLOR_PRINT, "client said: %s\n", ready_buf);

                    SERVER_LOG("文件内容:\n%s\n", file_buf);
                    write(client_fd, file_buf, file_len);
                    munmap(file_buf, file_len);
                    SERVER_LOG_COLOR(GREEN_COLOR_PRINT, "Sent file '%s' to client %s:%d, size: %d bytes\n", file_path,
                                tlibc_inet_ntoa(client_addr.sin_addr),
                                tlibc_ntohs(client_addr.sin_port),
                                file_len);
                    break;
                }
                data = (struct linux_dirent64 *)((char *)data + data->d_reclen); //< 遍历
            }
            if(data->d_off == 0){
                fdprintf(client_fd, "File %s not found in server directory.\n", buffer);
            }
            // fdprintf(client_fd, "You requested file: %s\n", buffer);
        }
    }
    
    if (bytes_received == 0) {
        SERVER_LOG("Client %s:%d disconnected\n",
                   tlibc_inet_ntoa(client_addr.sin_addr),
                   tlibc_ntohs(client_addr.sin_port));
    } else if (bytes_received < 0) {
        SERVER_LOG("recv() error");
    }
    close(client_fd);
    SERVER_LOG("\n");

    munmap(ls_buf, LS_BUF_SIZE);
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

    //初始化tserver文件夹
    char server_dir[256];
    tlibc_get_user_dir(server_dir, 256);
    strncat(server_dir, "/tserver", 255 - strlen(server_dir));
    int ret =__mkdirat(AT_FDCWD, server_dir, 0777);
    if (ret < 0) {
        if(ret == -EEXIST){
            SERVER_LOG("服务器目录%s已存在\n", server_dir);
        }
        else
        {
            SERVER_LOG("无法创建服务器目录%s, 错误码: %d\n", server_dir, ret);
            close(server_fd);
            return 1;
        }
    }
    strncat(server_dir, "/tserver_log.txt", 255 - strlen(server_dir));
    server_log_fd = openat(AT_FDCWD, server_dir, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(server_log_fd < 0){
        printf("无法打开日志文件, 错误码: %d\n", server_log_fd);
        return 1;
    }
    printf("打开日志文件成功, 日志文件描述符: %d\n", server_log_fd);

    SERVER_LOG("tserver在监听端口 %d\n", port);

    SERVER_LOG("Waiting for connections...\n\n");

    // 5. accept() 循环 - 接受客户端连接
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // 接受连接（阻塞）
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            SERVER_LOG("accept() failed");
            continue;
        }
        
        SERVER_LOG("New connection from %s:%d\n",
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
