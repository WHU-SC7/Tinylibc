/*
 * 025_prefix_inc.c — 前缀自增自减 ++x --x
 *
 * 验证：前缀递增/递减返回新值
 * 预期：退出码 0
 */

int main(void) {
    int x = 5;
    int y;

    /* ++x */
    y = ++x;
    if (x != 6) return 1;
    if (y != 6) return 2;

    /* --x */
    x = 10;
    y = --x;
    if (x != 9)  return 3;
    if (y != 9)  return 4;

    /* 表达式中的 ++x */
    x = 5;
    y = ++x + 10;
    if (x != 6)  return 5;
    if (y != 16) return 6;

    return 0;
}
