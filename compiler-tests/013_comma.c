/*
 * 013_comma.c — 逗号表达式
 *
 * 验证：逗号运算符顺序求值，结果为最后一个表达式
 * 预期：退出码 0
 */

int main(void) {
    int x;

    /* 基本逗号表达式 */
    x = (1, 2);
    if (x != 2) return 1;

    /* 副作用 + 最终值 */
    int a = 0;
    x = (a = 5, a + 1);
    if (x != 6) return 2;
    if (a != 5) return 3;

    /* 多个逗号 */
    x = (1, 2, 3, 4, 5);
    if (x != 5) return 4;

    /* 逗号在 for 中的用法（已在 007 中验证） */

    return 0;
}
