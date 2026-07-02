/*
 * 019_double_cmp.c — double 浮点比较
 *
 * 验证：double 的比较运算（SSE ucomisd）
 * 预期：退出码 0
 */

int main(void) {
    double x = 3.5;
    double y = 3.5;
    double z = 4.0;

    /* == */
    if (!(x == y)) return 1;
    if (x == z)    return 2;

    /* != */
    if (!(x != z)) return 3;
    if (x != y)    return 4;

    /* < */
    if (!(x < z))  return 5;
    if (z < x)     return 6;

    /* > */
    if (!(z > x))  return 7;
    if (x > z)     return 8;

    /* <= */
    if (!(x <= y)) return 9;
    if (!(x <= z)) return 10;
    if (z <= x)    return 11;

    /* >= */
    if (!(x >= y)) return 12;
    if (!(z >= x)) return 13;
    if (x >= z)    return 14;

    /* 与 0 比较 */
    double zero = 0.0;
    if (!(1.0 > zero)) return 15;
    if (zero > 1.0)    return 16;

    return 0;
}
