#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"

/**
 * @brief 格式化显示getdents64的内容，没有错误处理
 */
void print_getdents64_buf(struct linux_dirent64 *buf) //要求buf无数据部分是全0
{
    struct linux_dirent64 *data = buf;
    PRINT_COLOR(BRIGHT_CYAN_COLOR_PINRT, "off\tinode\ttype\tname\t\n");
    while (data->d_off != 0) //< 检查不严谨，但是考虑到每次list_file会清空ls_buf为0,这样是可以的
    {
        // printf("%d\t%d\t%d\t%s\n",data->d_off,data->d_ino,data->d_type,data->d_name);
        __printf("%d\t", data->d_off);
        __printf("%d\t", data->d_ino);
        switch (data->d_type)
        {
        case DT_DIR: //< 目录，蓝色
            PRINT_COLOR(BLUE_COLOR_PRINT, "DIR\t");
            PRINT_COLOR(BLUE_COLOR_PRINT, "%s\t", data->d_name);
            break;
        case DT_REG: //< 普通文件，白色
            __printf("FILE\t");
            __printf("%s\t", data->d_name);
            break;
        case DT_CHR: //< 字符设备，如console，黄色
            PRINT_COLOR(YELLOW_COLOR_PRINT, "CHA\t");
            PRINT_COLOR(YELLOW_COLOR_PRINT, "%s\t", data->d_name);
            break;
        case DT_BLK: //< 块设备，黄色
            PRINT_COLOR(YELLOW_COLOR_PRINT, "BLK\t");
            PRINT_COLOR(YELLOW_COLOR_PRINT, "%s\t", data->d_name);
            break;
        case DT_LNK: //< 符号链接，
            PRINT_COLOR(GREEN_COLOR_PRINT, "LNK\t");
            PRINT_COLOR(GREEN_COLOR_PRINT, "%s\t", data->d_name);
            break;
        default: //< 未知，红色
            PRINT_COLOR(RED_COLOR_PRINT, "%d\t", data->d_type);
            PRINT_COLOR(RED_COLOR_PRINT, "%s\t", data->d_name);
            break;
        }
        __printf("\n");
        // char *s=(char*)data; //<调试时逐个字节显示
        // for(int i=0;i<data->d_reclen;i++)
        // {
        //     printf("%d ",*s++);
        // }
        // printf("\n");
        data = (struct linux_dirent64 *)((char *)data + data->d_reclen); //< 遍历
    }
}

#define LS_BUF_SIZE 4096 //缓冲区大小
void ls(int argc, char *argv[])
{
    int open_ret;
    if(argc == 1) //没有给参数
    {
        open_ret = __openat(AT_FDCWD,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC,0644);
    }
    if(argc == 2) //一个参数
    {
        char *path = argv[1];
        __printf("参数: %s\n",path);
        open_ret = __openat(AT_FDCWD,path,O_RDONLY|O_DIRECTORY|O_CLOEXEC,0644);
    }
    if(argc > 2)
    {
        __printf("参数超过两个，太多了\n");
        __exit(-1);
    }
    char getdent_buf[LS_BUF_SIZE];
    for(int i=0;i<LS_BUF_SIZE;i++) //必须先清零
        getdent_buf[i]=0;
    if(open_ret < 0)
    {
        __printf("打开失败\n");
        __exit(-2);
    }
    __getdents64(open_ret,(struct linux_dirent64 *)getdent_buf, LS_BUF_SIZE);
    __close(open_ret);
    print_getdents64_buf((struct linux_dirent64 *)getdent_buf);
#if X86_64_TLIBC == 1
    LOG("很大off是正常的,可能是文件系统内部使用的哈希值。然后__printf只能输出int的数\n");
#endif
    __exit(0);
}