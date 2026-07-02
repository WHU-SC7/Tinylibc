/*
 * 005_if_else.c — if/else 条件分支
 *
 * 验证：if、if-else 控制流
 * 预期：退出码 0
 */

int main(void) {
    int x = 1;
    int y = 0;

    /* if (true) */
    if (x)
        y = 10;
    else
        y = 20;
    if (y != 10) return 1;

    /* if (false) */
    if (0)
        y = 30;
    else
        y = 40;
    if (y != 40) return 2;

    /* if without else */
    y = 0;
    if (x)
        y = 50;
    if (y != 50) return 3;

    /* nested if-else */
    int a = 1;
    int _b = 0;
    if (a)
        if (_b)
            y = 60;
        else
            y = 70;
    else
        y = 80;
    if (y != 70) return 4;

    return 0;
}
