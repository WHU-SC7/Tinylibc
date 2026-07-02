/*
 * 023_sizeof_expr.c — sizeof 表达式
 *
 * 验证：sizeof 作用于变量和表达式
 * 注意：sizeof(expr) 依赖 pvar_size 正确，复杂表达式默认 8
 * 预期：退出码 0
 */

int main(void) {
    int x;
    double d;
    int arr[10];

    if (sizeof(x)       != 4) return 1;
    if (sizeof(d)       != 8) return 2;
    if (sizeof(arr)     != 40) return 3;  /* 10 * 4 */

    return 0;
}
