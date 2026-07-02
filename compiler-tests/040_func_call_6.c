/*
 * 040_func_call_6.c — 6 参数函数调用（全寄存器传递）
 *
 * 验证：x86_64 ABI 前 6 个整数参数通过 rdi/rsi/rdx/rcx/r8/r9 传递
 * 预期：退出码 0
 */

int sum6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

int main(void) {
    int r = sum6(1, 2, 3, 4, 5, 6);
    if (r != 21) return 1;  /* 1+2+3+4+5+6 = 21 */

    r = sum6(10, 20, 30, 40, 50, 60);
    if (r != 210) return 2;

    return 0;
}
