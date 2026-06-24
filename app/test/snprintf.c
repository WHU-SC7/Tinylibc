#include "tlibc_everything.h"

int main(int argc, char *argv[])
{
    char buf[1024];
    memset(buf, 0, 1024);
    printf("Testing snprintf..., buf: %s\n", buf);
    snprintf(buf, 1024, "Hello %s, number: %d, long: %ld", "the string world", 42, 1234567890L);
    printf("buf: %s\n", buf);
    return 0;
}