/*
 * 078_complex_expr.c — 复杂混合表达式
 *
 * 验证：多种运算符混合的复杂表达式
 * 预期：退出码 0
 */

int main(void) {
    int x;

    /* 混合算术和位运算 */
    x = (1 + 2) * 3 ^ 4 & 0xFF;
    /* 9 ^ 4 = 13 */
    if (x != 13) return 1;

    /* 自增 + 算术 */
    int a = 5;
    int b = 10;
    int c = ++a + b;
    if (c != 16) return 2;  /* 6 + 10 */

    /* 三目嵌套运算 */
    int cond = 1;
    x = cond ? (a + b) : (a - b);
    if (x != 16) return 3;

    /* 运算符优先级综合 */
    /* 取自 C 优先级表 */
    x = 1 + 2 * 3 - 4 / 2;
    if (x != 5) return 4;  /* 1 + 6 - 2 = 5 */

    x = (1 + 2) * (3 - 4) / 2;
    if (x != -1) return 5; /* 3 * (-1) / 2 = -1 */

    return 0;
}
