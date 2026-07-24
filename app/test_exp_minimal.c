#include "tlibc_everything.h"
#include "math.h"

int main(void) {
    double x = exp(2.0);
    __printf("exp(2.0) = 0x%lx\n", *(unsigned long *)&x);
    return 0;
}
