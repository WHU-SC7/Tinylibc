/*
 * 044_recursion.c — 递归函数（阶乘）
 *
 * 验证：函数递归调用
 * 预期：退出码 0
 */

int fact(int n) {
    if (n <= 1)
        return 1;
    return n * fact(n - 1);
}

int main(void) {
    if (fact(0) != 1)   return 1;
    if (fact(1) != 1)   return 2;
    if (fact(5) != 120) return 3;

    /* 递归深度测试 */
    if (fact(10) != 3628800) return 4;

    return 0;
}
