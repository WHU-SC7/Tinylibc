#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"

void cat(int argc, char *argv[])
{
    if(argc != 2)
    {
        __printf("错误，需要一个参数!\n");
        __exit(-1);
    }
    if(argc >2)
    {
        __printf("参数超过两个，太多了\n");
        __exit(-5);
    }
    //获取大小然后输出
    if(*argv[1] == 0)
    {
        __printf("错误，传入空字符串!");
        __exit(-2);
    }
    int cat_fd = __openat(AT_FDCWD,argv[1],O_RDWR,0644); //有符号数不能用unsigned接收
    if(cat_fd < 0)
    {
        __printf("错误,打开文件%s失败\n",argv[1]);
        __exit(-3);
    }
    struct stat statbuf;
    char *ptr = (char *)&statbuf;
    for(int i=0; i<sizeof(struct stat); i++)
    {
        ptr[i] = 0;
    }
    int ret = __fstat(cat_fd,&statbuf);
    
    // 检查各个字段
    __printf("st_dev: %d\n", statbuf.st_dev);
    __printf("st_ino: %d\n", statbuf.st_ino); 
    __printf("st_mode: %d\n", statbuf.st_mode);
    __printf("st_size: %d\n", statbuf.st_size);

    unsigned long file_size = statbuf.st_size;
    if(ret != 0)
    {
        __printf("错误,fstat失败,返回值: %d\n", ret);
        __exit(-4);
    }
    __printf("fstat获取到文件大小: %d\n", file_size);
#define CAT_MAX_LEN 4096*4
    if(file_size > CAT_MAX_LEN)
    {
        __printf("文件内容大于%d, 将不显示文件内容\n", CAT_MAX_LEN);
        __exit(-5);
    }
    char cat_buf[CAT_MAX_LEN];
    ret = __read(cat_fd, cat_buf, file_size); //根据长度读取文件内容然后输出
    cat_buf[file_size] = '\n';
    __write(1,cat_buf,file_size+1);
    __exit(0);
}