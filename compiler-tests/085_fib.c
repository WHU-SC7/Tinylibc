/*
 * 085_fib.c — 递归斐波那契（性能基准 / 递归深度）
 *
 * 验证：递归函数性能和深度
 * fib(25) = 75025
 * 预期：退出码 0
 */

int fib(int n) {
    if (n <= 1)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    int r = fib(20);
    if (r != 6765) return 1;

    r = fib(25);
    if (r != 75025) return 2;

    return 0;
}
