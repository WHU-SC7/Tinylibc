/*
 * 029_array_decl.c — 数组声明与初始化
 *
 * 验证：数组声明、初始化和元素访问
 * 预期：退出码 0
 */

int main(void) {
    int arr[5];
    int i;

    /* 逐个赋值 */
    for (i = 0; i < 5; i = i + 1)
        arr[i] = i * 10;

    /* 验证 */
    if (arr[0] != 0)  return 1;
    if (arr[1] != 10) return 2;
    if (arr[2] != 20) return 3;
    if (arr[3] != 30) return 4;
    if (arr[4] != 40) return 5;

    /* 数组名作为指针 */
    if (*arr != 0) return 6;

    return 0;
}
