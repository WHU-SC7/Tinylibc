#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

void cp(int argc, char *argv[])
{
    if(argc < 3)
    {
        __printf("缺少参数\n");
        __exit(-1);
    }
    if(argc > 3)
    {
        __printf("参数超过三个，太多了\n");
        __exit(-2);
    }

    int open_ret = __openat(AT_FDCWD, argv[1], O_RDONLY, 0644);
    struct stat statbuf;
    char *ptr = (char *)&statbuf;
    for(int i=0; i<sizeof(struct stat); i++)
    {
        ptr[i] = 0;
    }
    int ret = __fstat(open_ret,&statbuf);
    unsigned long file_size = statbuf.st_size;
    if(ret != 0)
    {
        __printf("错误,fstat失败,返回值: %d\n", ret);
        __exit(-3);
    }

    int cp_fd = __creat(argv[2],0644); //创建目标复制文件
    char buf[4096]; //复制缓冲区
    //开始复制
    int count = file_size;
    while(count > 0) //一次复制4096字节
    {
        if(count < 4096) //不足的情况
        {
            __read(open_ret, buf, count);
            __write(cp_fd, buf, count);
            break;
        }
        __read(open_ret, buf, 4096);
        __write(cp_fd, buf, 4096);
        count -= 4096;
    }
    __exit(0);
}