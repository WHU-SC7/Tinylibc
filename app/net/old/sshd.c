#include "core.h"
#include "tlibc_print.h"
#include "errno.h"
#include "pthread.h"
#include "string.h"
#include "net.h"
#include "socket.h"
#include "tlibc_types.h"
#include "tlibc_everything.h"

#define BUFFER_SIZE 1024

struct client_arg {
    int client_fd;
    struct sockaddr_in client_addr;
    char password[256];
};

/* 认证：发送 "Password: " 提示，接收并校验密码 */
static int authenticate(int fd, const char *expected_pass)
{
    char buf[BUFFER_SIZE];
    int n;

    /* 发送密码提示 */
    write(fd, "Password: ", 10);

    /* 接收客户端密码 */
    n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return 0;
    buf[n] = '\0';

    /* 去掉末尾换行/回车 */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    if (strcmp(buf, expected_pass) == 0) {
        write(fd, "OK\n", 3);
        return 1;
    }

    write(fd, "FAIL\n", 5);
    return 0;
}

/* 每个客户端一个线程：认证 → fork shell → 等待结束 */
void *client_handler(void *arg)
{
    struct client_arg *carg = (struct client_arg *)arg;
    int fd = carg->client_fd;

    /* 认证 */
    if (!authenticate(fd, carg->password)) {
        printf("Authentication FAILED from %s:%d\n",
               tlibc_inet_ntoa(carg->client_addr.sin_addr),
               tlibc_ntohs(carg->client_addr.sin_port));
        close(fd);
        munmap(carg, sizeof(struct client_arg));
        return NULL;
    }

    printf("Authentication OK from %s:%d\n",
           tlibc_inet_ntoa(carg->client_addr.sin_addr),
           tlibc_ntohs(carg->client_addr.sin_port));

    /* fork 执行 shell */
    pid_t pid = fork();
    if (pid < 0) {
        printf("fork() failed\n");
        close(fd);
        munmap(carg, sizeof(struct client_arg));
        return NULL;
    }

    if (pid == 0) {
        /* ---- 子进程 ---- */
        int i;

        /* 将 socket fd 重定向到 stdin/stdout/stderr */
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);

        /* 关闭其他所有 fd（避免泄漏 listen fd 等） */
        for (i = 3; i < 256; i++)
            close(i);

        /* 执行系统 shell */
        char *shell_argv[] = {"/bin/sh", NULL};
        execve("/bin/sh", shell_argv, global_envp);

        /* execve 失败时才走到这里 */
        write(2, "execve: failed to start /bin/sh\n", 32);
        exit(1);
    }

    /* ---- 父线程 ---- */
    {
        int status;
        waitpid(pid, &status, 0);
    }

    printf("Session ended for %s:%d\n",
           tlibc_inet_ntoa(carg->client_addr.sin_addr),
           tlibc_ntohs(carg->client_addr.sin_port));

    close(fd);
    munmap(carg, sizeof(struct client_arg));
    return NULL;
}

int main(int argc, char *argv[])
{
    char password[256];

    if (argc < 2) {
        printf("Usage: %s <port> [<password>]\n", argv[0]);
        printf("  Default password (no argument): tinylibc\n");
        printf("  Or read from ~/.ssh_password\n");
        return 1;
    }

    int port = tlibc_strtoul(argv[1]);

    /* ---- 确定密码 ---- */
    if (argc >= 3) {
        strncpy(password, argv[2], sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';
    } else {
        /* 尝试从 ~/.ssh_password 读取 */
        char user_dir[256];
        if (tlibc_get_user_dir(user_dir, sizeof(user_dir)) == 0) {
            char pass_file[512];
            int len;

            len = snprintf(pass_file, sizeof(pass_file),
                           "%s/.ssh_password", user_dir);
            if (len > 0 && len < (int)sizeof(pass_file)) {
                int pfd = openat(AT_FDCWD, pass_file, O_RDONLY, 0);
                if (pfd >= 0) {
                    int n = read(pfd, password, sizeof(password) - 1);
                    if (n > 0) {
                        password[n] = '\0';
                        /* 去掉末尾换行 */
                        while (n > 0 && (password[n - 1] == '\n' ||
                                        password[n - 1] == '\r'))
                            password[--n] = '\0';
                    }
                    close(pfd);
                } else {
                    strncpy(password, "tinylibc", sizeof(password) - 1);
                    password[sizeof(password) - 1] = '\0';
                }
            } else {
                strncpy(password, "tinylibc", sizeof(password) - 1);
                password[sizeof(password) - 1] = '\0';
            }
        } else {
            strncpy(password, "tinylibc", sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
        }
    }

    printf("Starting sshd on port %d\n", port);

    /* ---- socket ---- */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("socket() failed\n");
        return 1;
    }

    /* SO_REUSEADDR */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("setsockopt() failed\n");
        close(server_fd);
        return 1;
    }

    /* bind */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = tlibc_htons(port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("bind() failed\n");
        close(server_fd);
        return 1;
    }

    /* listen */
    if (listen(server_fd, 5) < 0) {
        printf("listen() failed\n");
        close(server_fd);
        return 1;
    }

    printf("sshd listening on port %d, pid=%d\n", port, getpid());
    printf("Press Ctrl+C to stop\n");

    /* ---- accept 主循环 ---- */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) {
            printf("accept() failed\n");
            continue;
        }

        printf("New connection from %s:%d\n",
               tlibc_inet_ntoa(client_addr.sin_addr),
               tlibc_ntohs(client_addr.sin_port));

        struct client_arg *carg = mmap(0, sizeof(struct client_arg),
                                       PROT_READ | PROT_WRITE,
                                       MAP_ANONYMOUS | MAP_PRIVATE,
                                       -1, 0);
        carg->client_fd = client_fd;
        carg->client_addr = client_addr;
        strncpy(carg->password, password, sizeof(carg->password) - 1);
        carg->password[sizeof(carg->password) - 1] = '\0';

        pthread_t thread;
        pthread_create(&thread, NULL, client_handler, carg);
    }

    close(server_fd);
    return 0;
}
