/*
 * 063_double_to_int.c — double → int 转换
 *
 * 验证：double 赋值给 int 时截断
 * 预期：退出码 0
 */

int main(void) {
    int x;

    x = 3.14;
    if (x != 3) return 1;  /* 截断而非四舍五入 */

    x = 7.99;
    if (x != 7) return 2;

    x = -2.7;
    if (x != -2) return 3;

    /* int → double */
    double d;
    d = 5;
    if (d != 5.0) return 4;

    return 0;
}
