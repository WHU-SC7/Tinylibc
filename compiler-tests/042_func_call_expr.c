/*
 * 042_func_call_expr.c — 函数调用作为表达式
 *
 * 验证：函数调用结果直接参与运算
 * 预期：退出码 0
 */

int add(int a, int b) {
    return a + b;
}

int mul(int a, int b) {
    return a * b;
}

int main(void) {
    int x;

    /* 函数结果参与运算 */
    x = add(3, 4) * 2;
    if (x != 14) return 1;

    /* 函数作为函数参数 */
    x = add(add(1, 2), add(3, 4));
    if (x != 10) return 2;  /* (1+2)+(3+4) = 10 */

    /* 多层嵌套 */
    x = mul(add(2, 3), add(4, 5));
    if (x != 45) return 3;  /* (2+3)*(4+5) = 45 */

    return 0;
}
