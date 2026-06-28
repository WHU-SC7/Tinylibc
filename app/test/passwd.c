#include "tlibc_everything.h"

int main(int argc, char *argv[]){
    uid_t uid = getuid();
    printf("当前用户的UID是: %d\n", uid);
    int fd = openat(AT_FDCWD, "/etc/passwd", O_RDONLY, 0644);
    if(fd < 0){
        printf("无法打开/etc/passwd, 错误码: %d\n", fd);
        return 1;
    }
    char buf[4*1024];
    int n = read(fd, buf, sizeof(buf) - 1);
    if(n < 0){
        printf("读取/etc/passwd失败, 错误码: %d\n", n);
        close(fd);
        return 1;
    }
    buf[n] = 0; //确保是字符串
    printf("/etc/passwd内容:\n%s\n", buf);

    char *line_start = buf;
    char *line_end;
    char usr_name[64];
    while(*line_start){
        line_end = strchr(line_start, '\n');
        if(line_end == NULL){
            line_end = line_start + strlen(line_start); //最后一行没有换行符
        }
        int line_len = line_end - line_start;
        char *line = (char *)tlibc_malloc(line_len + 1);
        strncpy(line, line_start, line_len);
        line[line_len] = 0; //确保是字符串

        // 解析passwd格式: username:password:uid:gid:gecos:home:shell
        char *ptr = line;
        printf("解析行: %s\n", line);
        for(int i = 0; i < 7; i++){
            char *field_end = strchr(ptr, ':');
            if(field_end == (void *)-1){
                field_end = ptr + strlen(ptr); //最后一个字段没有冒号
            }
            int field_len = field_end - ptr;
            char name_buf[64];
            if(i == 0){ //用户名
                strncpy(name_buf, ptr, field_len);
                name_buf[field_len] = 0;
            }
            if(i == 2){ //UID字段
                char uid_str[16];
                strncpy(uid_str, ptr, field_len);
                uid_str[field_len] = 0;
                if(tlibc_strtoul(uid_str) == uid){
                    printf("当前用户的用户名是: %s\n", name_buf);
                    strncpy(usr_name, name_buf, 63);
                    usr_name[63] = 0;
                    break;
                }
            }
            ptr = field_end + 1; //跳过冒号
        }
        tlibc_free(line); //释放内存
        line_start = line_end + 1;
    }

    return 0;
}