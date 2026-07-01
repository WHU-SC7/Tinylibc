#include "tcc.h"
#include "elf.h"
#include "lex.c"
#include "parse.c"
#include "preproc.c"

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
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
