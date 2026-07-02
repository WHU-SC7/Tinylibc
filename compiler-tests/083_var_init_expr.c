/*
 * 083_var_init_expr.c — 变量初始化用表达式
 *
 * 验证：声明时用表达式初始化
 * 预期：退出码 0
 */

int add(int a, int b) {
    return a + b;
}

int main(void) {
    int x = 5;
    int y = x + 10;
    if (y != 15) return 1;

    int z = add(x, y);
    if (z != 20) return 2;

    /* 链式初始化 */
    int a = 1, b = a + 1, c = b + 1;
    if (a != 1) return 3;
    if (b != 2) return 4;
    if (c != 3) return 5;

    /* 多表达式初始化 */
    int arr[3] = {1, 2, 3};
    if (arr[0] != 1) return 6;
    if (arr[1] != 2) return 7;
    if (arr[2] != 3) return 8;

    return 0;
}
