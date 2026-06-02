#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "pthread.h"
#include "mman.h"
//madvise系统调用
int main(int argc, char *argv[])
{
    void *addr = __mmap(NULL, 4096, 
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, 
               -1, 0);
    __madvise(addr, 4096, MADV_NORMAL);
    __madvise(addr, 4096, MADV_RANDOM);
    __madvise(addr, 4096, MADV_SEQUENTIAL);
    __madvise(addr, 4096, MADV_WILLNEED);
    __madvise(addr, 4096, MADV_DONTNEED);
    __madvise(addr, 4096, MADV_COLD);
    return 0;
}