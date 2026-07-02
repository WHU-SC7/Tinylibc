/*
 * 061_double_const.c — double 常量与科学计数法
 *
 * 验证：double 常量各种写法
 * 预期：退出码 0
 */

int main(void) {
    double a, b, c;

    /* 基本小数 */
    a = 3.14;
    if (a != 3.14) return 1;

    /* 科学计数法 */
    b = 1.5e3;
    if (b != 1500.0) return 2;

    b = 2.5e-2;
    if (b != 0.025) return 3;

    /* 整数被赋给 double */
    c = 42;
    if (c != 42.0) return 4;

    /* 常量运算 */
    double d = 1.0 + 2.5;
    if (d != 3.5) return 5;

    return 0;
}
