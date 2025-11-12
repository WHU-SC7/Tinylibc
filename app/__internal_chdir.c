#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

//这是shell的内置命令，不按父子进程的模式执行。而是以函数调用的方式进行
//因为子进程chdir不能改变父进程的cwd,不是我们想要的效果
//所以不会使用__exit(),而是return
int __internal_chdir(int argc, char *argv[])
{
    //检查参数
    if(argc == 1)
    {
        __printf("错误，缺少参数\n");
        return -1;
    }
    if(argc >2)
    {
        __printf("错误，参数超过两个，太多了\n");
        return -2;
    }
    if(*argv[1] == 0)
    {
        __printf("错误，传入路径是空\n");
        return -3;
    }
    //执行
    int ret = __chdir(argv[1]);
    if(ret < 0) //错误码处理
    {
        if(ret == -ENOENT)
        {
            __printf("错误，给定路径不存在\n");
            return -5;
        }
        if(ret == -ENOTDIR)
        {
            __printf("错误，给定路径不是文件夹\n");
            return -6;
        }
        __printf("错误,chdir系统调用失败,错误码: %d\n",ret);
        return -7;
    }
    return 0; //正常执行，结束
}