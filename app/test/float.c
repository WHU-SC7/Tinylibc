#include "core.h"
#include "pthread.h"
#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "mempool.h"

int main(int argc, char *argv[])
{
    __printf("float test: %f\n", 3.1415926);
    __printf("float test: %f\n", -0.000001);
    __printf("float test: %f\n", 123456789.123456789);
    __printf("float test: %f\n", -123456789.123456789);
    return 0;
}