/*
 * 043_func_ptr.c — 函数指针
 *
 * 验证：函数指针声明、赋值和调用
 * 预期：退出码 0
 */

int add(int a, int b) {
    return a + b;
}

int sub(int a, int b) {
    return a - b;
}

int main(void) {
    int (*fp)(int, int);

    fp = add;
    if (fp(10, 3) != 13) return 1;

    fp = sub;
    if (fp(10, 3) != 7)  return 2;

    /* 通过函数指针链式调用 */
    fp = add;
    int x = fp(fp(1, 2), fp(3, 4));
    if (x != 10) return 3;

    return 0;
}
