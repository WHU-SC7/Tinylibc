#include "core.h"
#include "tlibc_print.h"
#include "errno.h"

int main(int argc, char *argv[])
{
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
    int ret = __mkdirat(AT_FDCWD, argv[1], 0777);
    if(ret == 0)
    {
        __printf("创建文件夹%s成功\n", argv[1]);
        return 0;
    }
    else
    {
        //错误信息处理
        if(ret == -ENOENT)
        {
            __printf("文件%s不存在,删除失败\n", argv[1]);
            return -1;
        }
        __printf("创建文件夹%s失败,错误码: %d\n", argv[1], ret);
        return -1;
    }
    return -1;
}