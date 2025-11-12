#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

void echo(int argc, char *argv[])
{
    // 支持echo "字符串"和echo "字符串" > 文件
    if(argc == 2)
    {
        if(*argv[1] == 0)
        {
            __printf("错误，传入空字符串\n");
            __exit(-1);
        }
        char *ptr = argv[1];
        int count = 0;
        while(*ptr++)
            count++;
        __write(1,argv[1],count);
    }
    if(argc == 4)
    {
        if(*argv[2] != '>')
        {
            __printf("格式错误,只支持echo \"字符串\" > 文件");
            __exit(-2);
        }
        if(*argv[3] == 0)
        {
            __printf("错误,传入空文件名\n");
            __exit(-3);
        }
        int open_ret = __openat(AT_FDCWD,argv[3],O_RDWR,0644);
        if(open_ret < 0)
        {
            if(open_ret == -ENOENT)
            {
                __printf("错误，文件%s不存在\n", argv[3]);
                __exit(-4);
            }
            __printf("打开文件失败\n");
            __exit(-5);
        }
        char *ptr = argv[1];
        int count = 0;
        while(*ptr++)
            count++;
        __write(open_ret, argv[1], count);
    }
    __write(1,"\n",1);
    __exit(0);
}