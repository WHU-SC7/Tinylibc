#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

void pwd(int argc, char *argv[])
{
    // 不管参数
    char buf[256];
    for(int i=0; i<256; i++)
        buf[i]=0;
    char *ret = __getcwd(buf,256);
    if(ret < 0)//不太可能失败吧
        panic("getcwd系统调用失败,错误码: %d\n",ret);
    __printf("%s\n",buf);
    __exit(0);
}