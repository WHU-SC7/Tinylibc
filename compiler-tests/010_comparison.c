/*
 * 010_comparison.c — 比较运算符 == != < > <= >=
 *
 * 验证：6 种关系运算
 * 预期：退出码 0
 */

int main(void) {
    int x = 5;
    int y = 10;

    /* == */
    if (!(5 == 5))    return 1;
    if (5 == 6)       return 2;

    /* != */
    if (!(5 != 6))    return 3;
    if (5 != 5)       return 4;

    /* < */
    if (!(x < y))     return 5;
    if (y < x)        return 6;
    if (5 < 5)        return 7;

    /* > */
    if (!(y > x))     return 8;
    if (x > y)        return 9;
    if (5 > 5)        return 10;

    /* <= */
    if (!(x <= y))    return 11;
    if (!(x <= 5))    return 12;
    if (10 <= x)      return 13;

    /* >= */
    if (!(y >= x))    return 14;
    if (!(y >= 10))   return 15;
    if (x >= 10)      return 16;

    /* 链式比较（a < b 的结果是 0 或 1，所以 (a < b < c) 不是数学3向比较） */
    int r = (1 < 5);
    if (r != 1)       return 17;
    r = (5 < 1);
    if (r != 0)       return 18;

    return 0;
}
