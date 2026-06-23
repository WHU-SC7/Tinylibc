#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"
#include "tty.h"

#define BUFFER_SIZE 4096

static int g_sock = -1;

/* SIGINT 处理：退出前恢复终端 raw 模式 */
static void sigint_handler(int signum)
{
    tlibc_restore_term(0);
    exit_group(128 + signum);
}

/* 接收线程：从 socket 读取 → 写入 stdout */
void *reader_thread(void *arg)
{
    char buf[BUFFER_SIZE];
    (void)arg;

    while (1) {
        int n = recv(g_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            /* 服务器断开连接 */
            break;
        }
        write(1, buf, n);
    }

    /* 终止整个进程（所有线程） */
    printf("\nConnection closed by server.\n");
    exit_group(0);
    return NULL; /* never reached */
}

int main(int argc, char *argv[])
{
    char password[256];
    char buf[BUFFER_SIZE];
    int n;

    if (argc < 3) {
        printf("Usage: %s <server_ip> <port> [<password>]\n", argv[0]);
        printf("Example: %s 127.0.0.1 2222\n", argv[0]);
        return 1;
    }

    char *server_addr = argv[1];
    int port = tlibc_strtoul(argv[2]);

    /* ---- 获取密码（命令行参数或交互输入） ---- */
    if (argc >= 4) {
        strncpy(password, argv[3], sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';
    } else {
        printf("password: ");
        n = read(0, password, sizeof(password) - 1);
        if (n > 0) {
            password[n] = '\0';
            while (n > 0 && (password[n - 1] == '\n' ||
                            password[n - 1] == '\r'))
                password[--n] = '\0';
        }
    }

    /* ---- socket + connect ---- */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket() failed\n");
        return 1;
    }
    g_sock = sock;

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = tlibc_htons(port);
    server.sin_addr.s_addr = tlibc_inet_addr(server_addr);

    printf("Connecting to %s:%d ...\n", server_addr, port);
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("connect() failed\n");
        close(sock);
        return 1;
    }
    printf("Connected.\n");

    /* ---- 认证 ---- */
    /* 接收 "Password: " 提示（作为协议握手，静默处理） */
    n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        printf("Server closed connection.\n");
        close(sock);
        return 1;
    }

    /* 发送密码 + 换行 */
    {
        char send_buf[512];
        n = snprintf(send_buf, sizeof(send_buf), "%s\n", password);
        if (n > 0)
            write(sock, send_buf, n);
    }

    /* 接收认证结果 */
    n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        printf("Server closed connection.\n");
        close(sock);
        return 1;
    }
    buf[n] = '\0';

    if (strcmp(buf, "OK\n") == 0) {
        printf("Authentication successful, starting session...\n");
        printf("$ ");
    } else {
        printf("Authentication failed.\n");
        close(sock);
        return 1;
    }

    /* ---- 交互式 Shell ---- */
    tlibc_sigaction(2, sigint_handler);  /* SIGINT -> 恢复终端 */
    tlibc_set_term_raw_and_noecho(0);    /* raw 模式 */

    pthread_t rthread;
    pthread_create(&rthread, NULL, reader_thread, NULL);

    /* 主线程：stdin -> socket（带本地回显） */
    {
        char input_buf[BUFFER_SIZE];
        while (1) {
            n = read(0, input_buf, sizeof(input_buf));
            if (n <= 0) {
                /* stdin 关闭（管道 EOF / Ctrl+D）：关闭写方向，通知服务端 */
                shutdown(sock, SHUT_WR);
                break;
            }

            /* 本地回显：用户在 raw 模式下看不到输入，由客户端回显 */
            {
                int i;
                for (i = 0; i < n; i++) {
                    unsigned char c = (unsigned char)input_buf[i];
                    if (c == '\r' || c == '\n') {
                        write(1, "\r\n", 2);
                    } else if (c == '\b' || c == 0x7f) {
                        write(1, "\b \b", 3);
                    } else if (c >= 32 && c < 127) {
                        write(1, &c, 1);
                    }
                    /* 其他控制字符（< 32, \t 除外）：静默跳过 */
                }
            }

            write(sock, input_buf, n);
        }
    }

    /* ---- 清理 ---- */
    tlibc_restore_term(0);

    /* 不在这里 close(sock) —— reader_thread 可能还在 recv */
    /* reader_thread 检测到服务端关闭后会调用 exit_group(0) 终止整个进程 */
    for (;;)
        msleep(3600000);

    return 0; /* never reached */
}
