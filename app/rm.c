#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

void rm(int argc, char *argv[])
{
    if(argc == 1)
    {
        __printf("缺少参数\n");
        __exit(-1);
    }
    if(argc >2)
    {
        __printf("参数超过两个，太多了\n");
        __exit(-2);
    }
    int ret = __unlinkat(AT_FDCWD,argv[1],0);
    if(ret == 0)
    {
        __printf("删除文件%s成功\n", argv[1]);
        __exit(0);
    }
    else
    {
        //错误信息处理
        if(ret == -ENOENT)
        {
            __printf("文件%s不存在,删除失败\n", argv[1]);
            __exit(-3);
        }
        if(ret == -EISDIR)
        {
            __printf("路径%s是一个文件夹, 删除失败\n");
            __exit(-6);
        }
        __printf("删除失败,错误码: %d\n", ret);
        __exit(-4);
    }
    __exit(-5);
}