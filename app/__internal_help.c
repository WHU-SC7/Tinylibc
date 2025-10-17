#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

int __internal_help(int argc, char *argv[])
{
    __printf("Tlibc shell, 版本 0.1\n");
    __printf("下面是可用的命令列表:\n");
    __printf("cat\ncp\necho\nls\nmkdir\nmv\npwd\nrm\nrmdir\ntouch\n"); //如果再添加命令，另起一行吧
    __printf("下面是可用的shell内置命令列表:\n");
    __printf("cd\nhelp\n");
    return 0;
}