/*
 * 006_while.c — while 循环
 *
 * 验证：while 循环结构和累计
 * 预期：退出码 0
 */

int main(void) {
    int i = 0;
    int sum = 0;

    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }

    /* sum = 0+1+2+...+9 = 45 */
    if (sum != 45) return 1;
    if (i != 10)   return 2;

    /* while (false) — 循环体不执行 */
    sum = 100;
    while (0) {
        sum = 200;
    }
    if (sum != 100) return 3;

    return 0;
}
