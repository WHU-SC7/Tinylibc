#include "tlibc_everything.h"

int tlibc_get_file_len(char *path)
{
    int fd = openat(AT_FDCWD, path, O_RDONLY, 0644);
    if(fd < 0)
    {
        return -1;
    }
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    int ret = fstat(fd, &statbuf);
    if(ret != 0)
    {
        return -1;
    }
    return statbuf.st_size;
}

int tlibc_get_file_count(const char *dir_path) {
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    #define COUNT_BUF_SIZE 1024*1024
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
                file_count++;
            }
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
    }
    
    munmap(buf, COUNT_BUF_SIZE);
    close(dir_fd);
    return file_count;
}

//获取目录下的文件名列表，由调用者获取文件个数并分配字符串的内存，约定每个字符串长度不超过255
//返回获取到的文件名个数
int tlibc_get_file_name_list(const char *dir_path, uint64_t file_name_list, int file_count) {
    if(file_count <= 0){
        return -1; //文件个数必须大于0
    }
    int dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0644);
    if (dir_fd < 0) {
        return -1;  // 打开目录失败
    }
    
    #define NAME_LIST_BUF_SIZE 1024*1024
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
        while ((char *)data < buf + ret) {
            // 排除 . 和 .. 目录
            if (strcmp(data->d_name, ".") != 0 && strcmp(data->d_name, "..") != 0) {
                // 将文件名复制到调用者分配的内存中
                strncpy(*file_name_ptr, data->d_name, 255);
                (*file_name_ptr)[255] = '\0'; // 确保字符串以null结尾
                file_name_ptr++; // 移动到下一个指针位置
                current_count++;
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