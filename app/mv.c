#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

void mv(int argc, char *argv[])
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
    int ret = __rename(argv[1],argv[2]);
    if(ret < 0)
    {
        __printf("rename失败,返回值: %d\n", ret);
        __exit(-3);
    }
    __exit(0);
}