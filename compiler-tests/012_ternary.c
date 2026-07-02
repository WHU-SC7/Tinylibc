/*
 * 012_ternary.c — 三目运算符 ? :
 *
 * 验证：条件表达式求值
 * 预期：退出码 0
 */

int main(void) {
    int x;

    /* 基本三目 */
    x = (1 ? 10 : 20);
    if (x != 10) return 1;

    x = (0 ? 10 : 20);
    if (x != 20) return 2;

    /* 作为子表达式 */
    if ((1 ? 5 : 6) != 5) return 3;
    if ((0 ? 5 : 6) != 6) return 4;

    /* 嵌套三目 */
    int a = 1;
    int _b = 0;
    x = a ? (_b ? 1 : 2) : 3;
    if (x != 2) return 5;

    x = 0 ? 1 : 2 ? 3 : 4;
    if (x != 3) return 6;

    /* 仅求值一个分支 */
    int side = 0;
    x = 1 ? 10 : (side = 1);
    if (side != 0) return 7;

    return 0;
}
