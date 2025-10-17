#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"

int __internal_help(int argc, char *argv[])
{
    __printf("Tlibc shell, 版本 0.1\n");
    __printf("下面是可用的命令列表:\n");
    __printf("文件操作:\n");
    __printf("  cat [file]                  - 显示文件内容\n");
    __printf("  cp [in] [out]               - 复制文件\n");
    __printf("  touch [file]                - 创建空文件\n");
    __printf("  rm [file]                   - 删除文件\n\n");

    __printf("目录操作:\n");
    __printf("  ls                          - 列出当前目录内容\n");
    __printf("  ls [dir]                    - 列出目录内容\n");
    __printf("  mkdir [dir]                 - 创建目录\n");
    __printf("  rmdir [dir]                 - 删除空目录\n");
    __printf("  pwd                         - 显示当前目录\n\n");

    __printf("其他:\n");
    __printf("  echo [text]                 - 输出文本\n");
    __printf("  echo [string] > [text]      - 输出文本\n");
    __printf("  mv [old] [new]              - 移动/重命名文件\n\n");

    __printf("下面是可用的shell内置命令列表:\n");
    __printf("  cd [dir]                    - 切换工作目录\n");
    __printf("  help                        - 帮助信息\n");
    return 0;
}