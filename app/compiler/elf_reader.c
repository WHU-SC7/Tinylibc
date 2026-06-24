#include "tlibc_everything.h"


//读取一个elf文件的所有内容
int main(int argc, char *argv[]){
    //打印参数
    for(int i = 0; i < argc; i++){
        printf("参数%d: %s\n", i, argv[i]);
    }
    if(argc != 2){
        printf("请指定文件名!\n");
        return -1;
    }
    printf("要读取的elf文件: %s\n", argv[1]);
    char *elf_path = argv[1];
    //逐个字节打印
    int fd = openat(AT_FDCWD, elf_path, O_RDWR, 0644);
    if(fd < 0){
        return -2;
    }
    int elf_len = tlibc_get_file_len(elf_path);
    if(elf_len < 0){
        return -3;
    }
    char *buf = tlibc_malloc(elf_len);
    char *read_ptr = buf;
    read(fd, buf, elf_len);
    // 逐个字节打印
    char hex_digits[] = "0123456789ABCDEF";
    for(int i = 0; i < elf_len; i++){
        unsigned char c = (unsigned char)buf[i];
        
        // 每16个字节打印地址偏移
        if(i % 16 == 0){
            if(i != 0) write(1, "\n", 1);
            write(1, "0x", 2);
            unsigned int addr = i;
            char addr_buf[8];
            for(int j = 7; j >= 0; j--){
                addr_buf[j] = hex_digits[addr & 0xF];
                addr >>= 4;
            }
            write(1, addr_buf, 8);
            write(1, ": ", 2);
        }
        
        // 判断是否英文字母或数字
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')){
            write(1, &c, 1);
        } else {
            write(1, "\\x", 2);
            char high = hex_digits[(c >> 4) & 0xF];
            char low = hex_digits[c & 0xF];
            write(1, &high, 1);
            write(1, &low, 1);
        }
        
        write(1, " ", 1);  // 字节间空格
    }
    write(1, "\n", 1);

    munmap(buf, elf_len);
}