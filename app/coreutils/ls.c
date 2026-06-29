#include "tlibc_everything.h"

/**
 * @brief 将 st_mode 解码为 "drwxrwxrwx" 权限字符串
 */
static void mode_to_str(mode_t mode, char *buf)
{
    /* 文件类型 */
    if (S_ISDIR(mode))       buf[0] = 'd';
    else if (S_ISCHR(mode))  buf[0] = 'c';
    else if (S_ISBLK(mode))  buf[0] = 'b';
    else if (S_ISFIFO(mode)) buf[0] = 'p';
    else if (S_ISLNK(mode))  buf[0] = 'l';
    else if (S_ISSOCK(mode)) buf[0] = 's';
    else                     buf[0] = '-';

    /* 所有者权限 */
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';

    /* 组权限 */
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';

    /* 其他权限 */
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';

    buf[10] = '\0';
}

/**
 * @brief 格式化显示一个文件的 ls -l 条目
 */
static void print_file_info(const char *dir_path, const char *name)
{
    struct stat st;
    char   full_path[1024];
    char   mode_str[12];
    char   time_buf[64];
    struct tm tm_buf;

    /* 构造完整路径 */
    int dlen = strlen(dir_path);
    if (dlen > 0 && dir_path[dlen - 1] == '/')
        snprintf(full_path, sizeof(full_path), "%s%s", dir_path, name);
    else
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);

    if (stat(full_path, &st) < 0) {
        __printf("? %s\n", name);
        return;
    }

    mode_to_str(st.st_mode, mode_str);

    /* 用 strftime 格式化修改时间 */
    gmtime_r(&st.st_mtim.tv_sec, &tm_buf);
    strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", &tm_buf);

    /* 输出：权限  nlink  uid  gid  大小  日期  文件名 */
    __printf("%s %3u %5d %5d %8ld %s %s\n",
             mode_str,
             (unsigned int)st.st_nlink,
             st.st_uid, st.st_gid,
             (long)st.st_size,
             time_buf, name);
}

/**
 * @brief 格式化显示 getdents64 的内容（无 -l 模式）
 */
static void print_getdents64_buf(struct linux_dirent64 *buf)
{
    struct linux_dirent64 *data = buf;
    PRINT_COLOR(BRIGHT_CYAN_COLOR_PRINT, "off\tinode\ttype\tname\n");
    while (data->d_off != 0) {
        __printf("%d\t", data->d_off);
        __printf("%d\t", data->d_ino);
        switch (data->d_type) {
        case DT_DIR:
            PRINT_COLOR(BLUE_COLOR_PRINT,   "DIR\t");
            PRINT_COLOR(BLUE_COLOR_PRINT,   "%s\n", data->d_name);
            break;
        case DT_REG:
            __printf("FILE\t%s\n", data->d_name);
            break;
        case DT_CHR:
            PRINT_COLOR(YELLOW_COLOR_PRINT, "CHA\t");
            PRINT_COLOR(YELLOW_COLOR_PRINT, "%s\n", data->d_name);
            break;
        case DT_BLK:
            PRINT_COLOR(YELLOW_COLOR_PRINT, "BLK\t");
            PRINT_COLOR(YELLOW_COLOR_PRINT, "%s\n", data->d_name);
            break;
        case DT_LNK:
            PRINT_COLOR(GREEN_COLOR_PRINT,  "LNK\t");
            PRINT_COLOR(GREEN_COLOR_PRINT,  "%s\n", data->d_name);
            break;
        default:
            PRINT_COLOR(RED_COLOR_PRINT,    "%d\t", data->d_type);
            PRINT_COLOR(RED_COLOR_PRINT,    "%s\n", data->d_name);
            break;
        }
        data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
    }
}

#define LS_BUF_SIZE 4096

int main(int argc, char *argv[])
{
    char *path = ".";
    int use_long = 0;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (char *p = argv[i] + 1; *p; p++) {
                if (*p == 'l')
                    use_long = 1;
            }
        } else {
            path = argv[i];
        }
    }

    /* 打开目录 */
    int fd = __openat(AT_FDCWD, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (fd < 0) {
        __printf("ls: 无法打开 '%s'\n", path);
        return 1;
    }

    char buf[LS_BUF_SIZE];
    for (int i = 0; i < LS_BUF_SIZE; i++)
        buf[i] = 0;

    int nread = __getdents64(fd, (struct linux_dirent64 *)buf, LS_BUF_SIZE);
    __close(fd);

    if (nread < 0) {
        __printf("ls: getdents64 失败\n");
        return 1;
    }

    if (use_long) {
        /* ls -l: 逐文件 stat + strftime */
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while (data->d_off != 0) {
            /* 跳过 . 和 .. */
            if (strcmp(data->d_name, ".")  == 0 ||
                strcmp(data->d_name, "..") == 0) {
                data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
                continue;
            }
            print_file_info(path, data->d_name);
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    } else {
        /* 默认模式：显示 inode 和类型 */
        print_getdents64_buf((struct linux_dirent64 *)buf);
    }

    return 0;
}
