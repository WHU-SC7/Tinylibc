/*
 * 076_nested_loop.c — 嵌套循环 + break/continue
 *
 * 验证：多层循环嵌套和 break/continue 控制流
 * 预期：退出码 0
 */

int main(void) {
    int sum = 0;
    int i, j;

    /* 嵌套循环累计 */
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 4; j = j + 1) {
            sum = sum + 1;
        }
    }
    if (sum != 12) return 1;  /* 3*4 = 12 */

    /* break 内层 */
    sum = 0;
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 10; j = j + 1) {
            if (j == 2) break;
            sum = sum + 1;
        }
    }
    if (sum != 6) return 2;  /* 3 * 2 = 6 */

    /* continue 内层 */
    sum = 0;
    for (i = 0; i < 5; i = i + 1) {
        if (i == 2) continue;
        sum = sum + i;
    }
    if (sum != 13) return 3;  /* 0+1+3+4 = 13 */

    return 0;
}
