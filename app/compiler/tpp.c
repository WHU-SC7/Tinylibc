/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * tpp — Tinylibc 独立预处理器
 *
 * 机制：读取 C 源文件，调用 preprocess() 做宏展开、头文件包含、条件编译，
 *       输出预处理后的文本。可独立于编译器使用，用于调试预处理结果。
 * 系统调用：openat, lseek, read, write, close
 *
 * 用法：
 *   tpp input.c output.i     # 预处理 input.c，输出到 output.i
 *
 * 索引：
 *   main            入口：读文件 → preprocess → 写结果
 */

#include "tcc.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        __write(2, "用法: tpp <输入.c> <输出.i>\n", 34);
        return 1;
    }
    add_include_path(".");
    add_include_path("./include");
    add_include_path("./include/posix");
    add_include_path("./include/tlibc");
    add_include_path("./arch");
    add_include_path("./arch/x86_64");
    
    int src_len;
    int fd = __openat(AT_FDCWD, argv[1], O_RDONLY, 0);
    off_t size = __lseek(fd, 0, SEEK_END); __lseek(fd, 0, SEEK_SET);
    char *src = (char *)tlibc_malloc(size + 2);
    __read(fd, src, size); __close(fd);
    src[size] = '\0'; src_len = size;
    
    int pp_len;
    char *pp = preprocess(src, src_len, argv[1], &pp_len);
    
    int out = __openat(AT_FDCWD, argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
    __write(out, pp, pp_len);
    __close(out);
    return 0;
}
