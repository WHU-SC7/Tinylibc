/*
 * 028_assign_chain.c — 链式赋值
 *
 * 验证：a = b = c 从右向左赋值
 * 预期：退出码 0
 */

int main(void) {
    int a, b, c;

    a = b = c = 42;
    if (a != 42) return 1;
    if (b != 42) return 2;
    if (c != 42) return 3;

    a = b = c = 10;
    if (a != 10) return 4;
    if (b != 10) return 5;
    if (c != 10) return 6;

    return 0;
}
