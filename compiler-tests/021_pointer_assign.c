/*
 * 021_pointer_assign.c — 指针变量赋值
 *
 * 验证：指针赋值和间接访问
 * 预期：退出码 0
 */

int main(void) {
    int x = 42;
    int *p, *q;

    p = &x;

    /* 指针读取 */
    if (*p != 42) return 1;

    /* 指针写入 */
    *p = 100;
    if (x != 100) return 2;

    /* 指针赋值给指针 */
    q = p;
    *q = 200;
    if (x != 200) return 3;

    /* NULL 指针赋值 */
    p = 0;
    if (p != 0) return 4;

    return 0;
}
