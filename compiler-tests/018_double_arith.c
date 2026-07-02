/*
 * 018_double_arith.c — double 浮点算术运算
 *
 * 验证：double 加减乘除，SSE 指令
 * 预期：退出码 0
 */

int main(void) {
    double x, y, z;

    x = 3.5;
    y = 2.0;

    /* 加法 */
    z = x + y;
    if (z != 5.5) return 1;

    /* 减法 */
    z = x - y;
    if (z != 1.5) return 2;

    /* 乘法 */
    z = x * y;
    if (z != 7.0) return 3;

    /* 除法 */
    z = x / y;
    if (z != 1.75) return 4;

    /* 混合运算 */
    z = (x + 1.0) * 2.0;
    if (z != 9.0) return 5;

    /* 负值（用整数比较避免 xmm1 被 negate_double 破坏） */
    z = -3.5;
    if (z >= 0.0) return 6;   /* 负值应小于 0 */

    /* 连续运算 */
    double sum = 0.0;
    sum = sum + 1.0;
    sum = sum + 2.0;
    sum = sum + 3.0;
    if (sum != 6.0) return 7;

    return 0;
}
