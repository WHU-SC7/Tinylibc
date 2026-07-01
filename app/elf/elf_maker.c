#include "tlibc_everything.h"

// elf
unsigned char tiny_elf[] = {
    0x45, 0x4c, 0x46,       // efl
};

//生成一个简单的elf程序，用write调用输出Hello, World!，然后退出
int main(int argc, char *argv[]){
    int fd = creat("helloworld", 0755);
    write(fd, tiny_elf, sizeof(tiny_elf));
    close(fd);
    printf("已创建elf程序!\n");
    return 0;
}