/*
 * 034_addr_of_local.c — & 取局部变量地址
 *
 * 验证：取局部变量的地址并赋值给指针
 * 预期：退出码 0
 */

int main(void) {
    int x = 42;
    int *p;

    p = &x;

    /* 通过指针读取 */
    if (*p != 42) return 1;

    /* 通过指针修改原变量 */
    *p = 100;
    if (x != 100) return 2;

    /* 两个变量地址 */
    int a = 1, b = 2;
    int *pa = &a;
    int *pb = &b;

    if (*pa != 1) return 3;
    if (*pb != 2) return 4;

    *pa = *pa + *pb;
    if (a != 3) return 5;

    return 0;
}
