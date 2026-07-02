/*
 * 007_for.c — for 循环
 *
 * 验证：for 三表达式循环
 * 预期：退出码 0
 */

int main(void) {
    int sum = 0;
    int i;

    /* 标准 for */
    for (i = 0; i < 10; i = i + 1) {
        sum = sum + i;
    }
    if (sum != 45) return 1;

    /* for with declaration */
    sum = 0;
    for (int j = 0; j < 5; j = j + 1) {
        sum = sum + j;
    }
    if (sum != 10) return 2;

    /* 空循环体 */
    for (i = 0; i < 5; i = i + 1)
        ;
    if (i != 5) return 3;

    return 0;
}
