/*
 * 059_static_func.c — static 函数
 *
 * 验证：static 函数的声明和调用
 * 预期：退出码 0
 */

static int internal_add(int a, int b) {
    return a + b;
}

static int internal_val = 42;

int main(void) {
    int r = internal_add(10, 20);
    if (r != 30) return 1;

    if (internal_val != 42) return 2;

    return 0;
}
