/*
 * 003_local_var.c — 局部变量声明与赋值
 *
 * 验证：变量声明、赋值、读取
 * 预期：退出码 0
 */

int main(void) {
    int a;
    a = 10;
    if (a != 10) return 1;

    int b = 20;
    if (b != 20) return 2;

    int c = a + b;
    if (c != 30) return 3;

    return 0;
}
