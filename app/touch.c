#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"

void touch(int argc, char *argv[])
{
    //先认为参数正确
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
    char *path = argv[1];
    int open_ret = __creat(path,0644);
    __close(open_ret);
    // __printf("openat返回值: %d\n",open_ret);
    __exit(0);
}