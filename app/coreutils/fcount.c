#include "tlibc_everything.h"

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }
    char dir_path[256];
    strncpy(dir_path, argv[1], 255);
    dir_path[255] = '\0';
    

    int file_count = tlibc_get_file_count(dir_path);
    if (file_count < 0) {
        printf("Failed to get file count for directory: %s\n", dir_path);
        return 1;
    }

    printf("File count in directory '%s': %d\n", dir_path, file_count);

    // 根据fcount获取文件名列表
    char **file_name_list = (char **)tlibc_malloc(file_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(file_count * 256);
    for(int i = 0; i < file_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    int ret = tlibc_get_file_name_list(dir_path, (uint64_t)file_name_list, file_count);
    for(int i = 0; i < ret; i++) {
        printf("File %d: %s\n", i, file_name_list[i]);
    }
    tlibc_free(file_name_list);
    tlibc_free(file_name_buf);
    return 0;
}