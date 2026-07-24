#include "tlibc_everything.h"
#include "math.h"

int main(void) {
    double x0 = exp(0.0);
    double x1 = exp(1.0);
    double x2 = exp(2.0);
    double xm1 = exp(-1.0);
    __printf("exp(0.0) = 0x%lx\n", *(unsigned long *)&x0);
    __printf("exp(1.0) = 0x%lx\n", *(unsigned long *)&x1);
    __printf("exp(2.0) = 0x%lx\n", *(unsigned long *)&x2);
    __printf("exp(-1.0) = 0x%lx\n", *(unsigned long *)&xm1);
    return 0;
}
