#include "tlibc_everything.h"

int tlibc_get_file_len(char *path)
{
    int fd = openat(AT_FDCWD, path, O_RDONLY, 0644);
    if(fd < 0)
    {
        return -1;
    }
    int save_fd = fd;
    
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    int ret = fstat(fd, &statbuf);
    if(ret != 0)
    {
        close(save_fd);
        return -1;
    }
    close(save_fd);
    return statbuf.st_size;
}

int tlibc_get_dir_count(const char *dir_path) {
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    #define COUNT_BUF_SIZE TLIBC_BUF_SIZE
    char *buf = (char *)mmap(0, COUNT_BUF_SIZE, PROT_READ | PROT_WRITE, 
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED) {
        close(dir_fd);
        return -1;
    }
    
    int dir_count = 0;
    int ret;
    
    while ((ret = getdents64(dir_fd, (struct linux_dirent64 *)buf, COUNT_BUF_SIZE)) > 0) {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while ((char *)data < buf + ret) {
            // 排除 . 和 .. 目录
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                char buf[1024];
                snprintf(buf, 1024, "%s/%s", dir_path, data->d_name);
                if(tlibc_is_path_dir(buf) == 1){ //确保是目录
                    dir_count++;
                }
                // printf("检查目录: %s, 是否是目录: %d\n", buf, tlibc_is_path_dir(buf));
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }
    
    munmap(buf, COUNT_BUF_SIZE);
    close(dir_fd);
    return dir_count;
}

int tlibc_get_file_count(const char *dir_path) {
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    #define COUNT_BUF_SIZE TLIBC_BUF_SIZE
    char *buf = (char *)mmap(0, COUNT_BUF_SIZE, PROT_READ | PROT_WRITE, 
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED) {
        close(dir_fd);
        return -1;
    }
    
    int file_count = 0;
    int ret;
    
    while ((ret = getdents64(dir_fd, (struct linux_dirent64 *)buf, COUNT_BUF_SIZE)) > 0) {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while ((char *)data < buf + ret) {
            // 排除 . 和 .. 目录
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                char file_path[1024];
                snprintf(file_path, 1024, "%s/%s", dir_path, data->d_name);
                if(tlibc_is_path_file(file_path) == 1){ //确保是文件
                    file_count++;
                }
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }
    
    munmap(buf, COUNT_BUF_SIZE);
    close(dir_fd);
    return file_count;
}

//获取目录下的目录名列表，由调用者获取目录个数并分配字符串的内存，约定每个字符串长度不超过255
//返回获取到的目录名个数，不包含.和..目录
int tlibc_get_dir_name_list(const char *dir_path, uint64_t dir_name_list, int dir_count) {
    if(dir_count <= 0){
        return -1; //目录个数必须大于0
    }
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    #define NAME_LIST_BUF_SIZE TLIBC_BUF_SIZE
    char *buf = (char *)mmap(0, NAME_LIST_BUF_SIZE, PROT_READ | PROT_WRITE, 
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED) {
        close(dir_fd);
        return -1;
    }
    
    int ret;
    int current_count = 0;
    char **dir_name_ptr = (char **)dir_name_list; //存储目录名的指针数组
    
    if ((ret = getdents64(dir_fd, (struct linux_dirent64 *)buf, NAME_LIST_BUF_SIZE)) > 0) {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while ((char *)data < buf + ret && current_count < dir_count) {
            // 排除 . 和 .. 目录
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                char buf[1024];
                snprintf(buf, 1024, "%s/%s", dir_path, data->d_name);
                if(tlibc_is_path_dir(buf) == 1){ //确保是目录
                    // 将目录名复制到调用者分配的内存中
                    strncpy(*dir_name_ptr, data->d_name, 255);
                    (*dir_name_ptr)[255] = '\0'; // 确保字符串以null结尾
                    dir_name_ptr++; // 移动到下一个指针位置
                    current_count++;
                }
                // printf("检查目录: %s, 是否是目录: %d\n", buf, tlibc_is_path_dir(buf));
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }
    else {
        munmap(buf, NAME_LIST_BUF_SIZE);
        close(dir_fd);
        return -1;  // 读取目录失败
    }
    
    munmap(buf, NAME_LIST_BUF_SIZE);
    close(dir_fd);
    return current_count;
}

//获取目录下的文件名列表，由调用者获取文件个数并分配字符串的内存，约定每个字符串长度不超过255
//返回获取到的文件名个数，不包含.和..目录
int tlibc_get_file_name_list(const char *dir_path, uint64_t file_name_list, int file_count) {
    if(file_count <= 0){
        return -1; //文件个数必须大于0
    }
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    #define NAME_LIST_BUF_SIZE TLIBC_BUF_SIZE
    char *buf = (char *)mmap(0, NAME_LIST_BUF_SIZE, PROT_READ | PROT_WRITE, 
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED) {
        close(dir_fd);
        return -1;
    }
    
    int ret;
    int current_count = 0;
    char **file_name_ptr = (char **)file_name_list; //存储文件名的指针数组
    
    if ((ret = getdents64(dir_fd, (struct linux_dirent64 *)buf, NAME_LIST_BUF_SIZE)) > 0) {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while ((char *)data < buf + ret && current_count < file_count) {
            // 排除 . 和 .. 目录
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                char file_path[1024];
                snprintf(file_path, 1024, "%s/%s", dir_path, data->d_name);
                if(tlibc_is_path_file(file_path) == 1){ //确保是文件
                    // 将文件名复制到调用者分配的内存中
                    strncpy(*file_name_ptr, data->d_name, 255);
                    (*file_name_ptr)[255] = '\0'; // 确保字符串以null结尾
                    file_name_ptr++; // 移动到下一个指针位置
                    current_count++;
                }
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }
    else {
        munmap(buf, NAME_LIST_BUF_SIZE);
        close(dir_fd);
        return -1;  // 读取目录失败
    }
    
    munmap(buf, NAME_LIST_BUF_SIZE);
    close(dir_fd);
    return current_count;
}

int tlibc_is_path_dir(const char *path){
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    int ret = stat(path, &statbuf);
    if (ret < 0) {
        return -1;  // 获取文件状态失败
    }
    return S_ISDIR(statbuf.st_mode);  // 判断是否为目录
}

//不存在返回-1，存在且是文件返回1，存在但不是文件返回0
int tlibc_is_path_file(const char *path){
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    int ret = stat(path, &statbuf);
    if (ret < 0) {
        return -1;  // 获取文件状态失败
    }
    return S_ISREG(statbuf.st_mode);  // 判断是否为普通文件
}

int tlibc_rm_file(const char *path){
    return unlinkat(AT_FDCWD, path, 0);
}

//递归删除一个目录，期望path是目录
int tlibc_recursive_rm_dir(const char *path){
    int ret = tlibc_is_path_dir(path);
    if(ret < 0){
        printf("路径%s不存在, 无法删除, ret: %d\n", path, ret);
        return -1; //路径不存在
    }

    if(ret == 1){ //可能要递归删除目录
        #define RM_BUF_SIZE TLIBC_BUF_SIZE
        char *buf = (char *)tlibc_malloc(RM_BUF_SIZE);
        if (buf == MAP_FAILED) {
            return -1;
        }
        int dir_fd = openat(AT_FDCWD, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
        if (dir_fd < 0) {
            munmap(buf, RM_BUF_SIZE);
            return -1;  // 打开目录失败
        }
        int ret;
        while ((ret = getdents64(dir_fd, (struct linux_dirent64 *)buf, RM_BUF_SIZE)) > 0) {
            struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
            while ((char *)data < buf + ret) {
                // 排除 . 和 .. 目录
                if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                    char sub_path[512];
                    memset(sub_path, 0, sizeof(sub_path));
                    strncpy(sub_path, path, 511);
                    strncat(sub_path, "/", 511 - strlen(sub_path));
                    strncat(sub_path, data->d_name, 511 - strlen(sub_path));
                    // printf("递归删除子路径: %s\n", sub_path);
                    //判断子路径是文件还是目录，如果是目录就递归删除，如果是文件直接删除
                    if(tlibc_is_path_file(sub_path) == 1){
                        int rm_ret = tlibc_rm_file(sub_path);
                        if(rm_ret < 0){
                            // printf("删除文件%s失败, 返回值: %d\n", sub_path, rm_ret);
                        }
                        else{
                            // printf("删除文件%s成功\n", sub_path);
                        }
                    }
                    else{
                        int rm_ret = tlibc_recursive_rm_dir(sub_path); //递归删除子路径
                        if(rm_ret < 0){
                            // printf("删除子路径%s失败, 返回值: %d\n", sub_path, rm_ret);
                        }
                        // printf("删除子路径%s成功\n", sub_path);
                    }
                }
                data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
            }
        }
        munmap(buf, RM_BUF_SIZE);
        close(dir_fd);
        // printf("删除已空目录: %s\n", path);
        return unlinkat(AT_FDCWD, path, AT_REMOVEDIR); //删除空目录
    }
    PRINT_COLOR(RED_COLOR_PRINT, "对path: %s的类型判断异常!", path);
    return 0;
}

int tlibc_recursive_mkdir(const char *path){
    char temp_path[512];
    memset(temp_path, 0, sizeof(temp_path));
    strncpy(temp_path, path, 511);
    char *p = temp_path;
    while(*p != '\0'){
        if(*p == '/'){
            *p = '\0';
            if(strlen(temp_path) > 0){ //避免路径以/开头时创建空目录
                int ret = mkdirat(AT_FDCWD, temp_path, 0755);
                if(ret < 0 && ret != -EEXIST){ //如果目录已存在也继续创建后续目录
                    printf("创建目录%s失败, 返回值: %d, 错误信息: %s\n", temp_path, ret, strerror(ret));
                    return -1;
                }
            }
            *p = '/';
        }
        p++;
    }
    //创建最后一级目录
    int ret = mkdirat(AT_FDCWD, temp_path, 0755);
    if(ret < 0 && ret != -EEXIST){
        printf("创建目录%s失败, 返回值: %d, 错误信息: %s\n", temp_path, ret, strerror(ret));
        return -1;
    }
    return 0;
}

/* Recursively counts all files under a directory (including files in subdirectories).
   Returns the total count, or -1 on error. Path must be a directory. */
int tlibc_recursive_count_file(const char *path)
{
    int ret = tlibc_is_path_dir(path);
    if (ret < 0) {
        return -1;  // Not found
    }
    if (ret == 0) {
        return 1;   // It's a file, count it
    }

    /* It's a directory — open and iterate */
    int dir_fd = openat(AT_FDCWD, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;
    }

    char *buf = (char *)mmap(0, TLIBC_BUF_SIZE, PROT_READ | PROT_WRITE,
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED) {
        close(dir_fd);
        return -1;
    }

    int total = 0;
    ssize_t n;
    while ((n = getdents64(dir_fd, (struct linux_dirent64 *)buf, TLIBC_BUF_SIZE)) > 0) {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while ((char *)data < buf + n) {
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                char sub_path[1024];
                snprintf(sub_path, sizeof(sub_path), "%s/%s", path, data->d_name);
                int sub_count = tlibc_recursive_count_file(sub_path);
                if (sub_count > 0) {
                    total += sub_count;
                }
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }

    munmap(buf, TLIBC_BUF_SIZE);
    close(dir_fd);
    return total;
}

//获取目录下的文件数量，路径不存在或不是目录返回-1
int tlibc_get_file_num(const char *dir_path){
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    char *buf = (char *)tlibc_malloc(DEFAULT_LS_BUF_SIZE);
    if (buf == NULL) {
        close(dir_fd);
        return -1;
    }
    
    int file_count = 0;
    int ret;
    
    while ((ret = getdents64(dir_fd, (struct linux_dirent64 *)buf, DEFAULT_LS_BUF_SIZE)) > 0) {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        while ((char *)data < buf + ret) {
            // 排除 . 和 .. 目录
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                file_count++;
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }
    
    munmap(buf, DEFAULT_LS_BUF_SIZE);
    close(dir_fd);
    return file_count;
}

/* Copies a regular file from src_path to dest_path (overwrites if exists). Returns 0 on success, or -1 on error */
int tlibc_copy_file(char *src_path, char *dest_path){
    int src_fd = openat(AT_FDCWD, src_path, O_RDONLY, 0644);
    if(src_fd < 0){
        printf("打开源文件%s失败, 错误码: %d\n", src_path, src_fd);
        return -1;
    }
    int dest_fd = openat(AT_FDCWD, dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(dest_fd < 0){
        printf("打开目标文件%s失败, 错误码: %d\n", dest_path, dest_fd);
        close(src_fd);
        return -1;
    }
    char buf[4096];
    int n;
    while((n = read(src_fd, buf, sizeof(buf))) > 0){
        if(write(dest_fd, buf, n) != n){
            printf("写入文件%s失败, 错误码: %d\n", dest_path, -EIO);
            close(src_fd);
            close(dest_fd);
            return -1;
        }
    }
    if(n < 0){
        printf("读取文件%s失败, 错误码: %d\n", src_path, n);
        close(src_fd);
        close(dest_fd);
        return -1;
    }
    close(src_fd);
    close(dest_fd);
    return 0;
}

/* Copies a file and sets the executable bit on the destination */
int tlibc_copy_exe_file(char *src_path, char *dest_path){
    tlibc_copy_file(src_path, dest_path);
    chmod(dest_path, 0755); //设置可执行权限
    return 0;
}