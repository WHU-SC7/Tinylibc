/*
 * 041_func_call_7.c — 7+ 参数函数调用（栈传递）
 *
 * 验证：第 7 个及以上参数通过栈传递（x86_64 ABI）
 * 预期：退出码 0
 */

int sum7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}

int sum8(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}

int main(void) {
    int r;

    r = sum7(1, 2, 3, 4, 5, 6, 7);
    if (r != 28) return 1;

    r = sum8(1, 2, 3, 4, 5, 6, 7, 8);
    if (r != 36) return 2;

    /* 更多参数 */
    r = sum7(10, 20, 30, 40, 50, 60, 70);
    if (r != 280) return 3;

    return 0;
}
