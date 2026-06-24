#include "tlibc_everything.h"

int main(int argc, char *argv[]){
    //测试tlibc_get_file_name_list能不能排除目录类型
    char *path = ".";
    int files_count = tlibc_get_file_count(path);
    printf("文件数量: %d\n", files_count);
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(path, (uint64_t)file_name_list, files_count);
    //打印
    for(int i=0; i<files_count; i++){
        printf("文件: %s\n", file_name_list[i]);
    }
    
    return 0;
}