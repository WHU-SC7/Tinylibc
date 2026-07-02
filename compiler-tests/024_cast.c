/*
 * 024_cast.c — 类型转换
 *
 * 验证：显式类型转换语法（tcc 可能不生成转换代码）
 * 注意：tcc 中 cast 不会实际生成转换指令（已知限制）
 * 预期：退出码 0
 */

int main(void) {
    double d;
    int x;

    /* double → int */
    d = 3.14;
    x = (int)d;     /* tcc: 可能不转换，d 值直接赋给 x */
    if (x != 3) return 1;  /* 应为 3（截断），但 tcc 已知 bug 可能不是 */

    /* int → double */
    x = 5;
    d = (double)x;  /* tcc: 可能不转换 */
    if (d != 5.0) return 2;

    /* 指针类型转换 */
    int a = 0x12345678;
    char *pc = (char *)&a;
    (void)pc;       /* 避免未使用警告 */

    return 0;
}
