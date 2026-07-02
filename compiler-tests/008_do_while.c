/*
 * 008_do_while.c — do-while 循环
 *
 * 验证：do-while 至少执行一次
 * 预期：退出码 0
 */

int main(void) {
    int i = 0;
    int sum = 0;

    do {
        sum = sum + i;
        i = i + 1;
    } while (i < 5);

    if (sum != 10) return 1;  /* 0+1+2+3+4 */
    if (i != 5)    return 2;

    /* 条件始终为 false 时也至少执行一次 */
    i = 42;
    do {
        i = 100;
    } while (0);
    if (i != 100) return 3;

    return 0;
}
