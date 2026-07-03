/*
 * 030_array_index.c — 数组下标访问 a[i]
 *
 * 验证：数组元素读取 hello
 * 预期：退出码 0
 */

int main(void) {
    int arr[5] = {10, 20, 30, 40, 50};

    if (arr[0] != 10) return 1;
    if (arr[1] != 20) return 2;
    if (arr[2] != 30) return 3;
    if (arr[3] != 40) return 4;
    if (arr[4] != 50) return 5;

    /* 变量下标 */
    int i = 2;
    if (arr[i] != 30) return 6;

    /* 表达式下标 */
    i = 1;
    if (arr[i + 2] != 40) return 7;

    /* 负数下标（C 允许，但如果 arr[0] 是基地址则 arr[-1] 越界）— 跳过 */
    /* 指针形式 */
    int *p = arr;
    if (*(p + 3) != 40) return 8;

    return 0;
}
