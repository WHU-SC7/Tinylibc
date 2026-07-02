/*
 * 062_double_func.c — double 作为函数参数和返回值
 *
 * 验证：double 参数通过 xmm0-xmm5 传递
 * 预期：退出码 0
 */

double add_double(double a, double b) {
    return a + b;
}

double mul_double(double a, double b) {
    return a * b;
}

int main(void) {
    double r;

    r = add_double(1.5, 2.5);
    if (r != 4.0) return 1;

    r = mul_double(3.0, 4.0);
    if (r != 12.0) return 2;

    /* 嵌套调用 */
    r = add_double(mul_double(2.0, 3.0), 1.0);
    if (r != 7.0) return 3;  /* (2*3)+1 = 7 */

    return 0;
}
