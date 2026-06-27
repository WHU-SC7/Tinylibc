#include "core.h"
#include "tlibc_print.h"
#include "errno.h"

int main(int argc, char *argv[])
{
    if(argc < 3)
    {
        __printf("缺少参数\n");
        return -1;
    }
    if(argc > 3)
    {
        __printf("参数超过三个，太多了\n");
        return -1;
    }
    int ret = __rename(argv[1],argv[2]);
    if(ret < 0)
    {
        __printf("rename失败,返回值: %d\n", ret);
        return -1;
    }
    return 0;
}