/*
 * 004_arith.c — 整数算术运算 + - * / %
 *
 * 验证：5 种基本算术运算符
 * 预期：退出码 0
 */

int main(void) {
    if (2 + 3 != 5)  return 1;
    if (7 - 4 != 3)  return 2;
    if (3 * 4 != 12) return 3;
    if (13 / 5 != 2) return 4;
    if (13 % 5 != 3) return 5;

    /* 复合表达式 */
    if ((1 + 2) * 3 != 9) return 6;
    if (10 - 3 - 2 != 5)  return 7;

    return 0;
}
