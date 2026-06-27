#include "core.h"
#include "tlibc_print.h"

int main(int argc, char *argv[])
{
    //先认为参数正确
    if(argc == 1)
    {
        __printf("缺少参数\n");
        return -1;
    }
    if(argc >2)
    {
        __printf("参数超过两个，太多了\n");
        return -1;
    }
    char *path = argv[1];
    int open_ret = __creat(path,0644);
    __close(open_ret);
    // __printf("openat返回值: %d\n",open_ret);
    return 0;
}