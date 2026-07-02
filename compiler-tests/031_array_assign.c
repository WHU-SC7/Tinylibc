/*
 * 031_array_assign.c — 数组元素赋值 a[i] = v
 *
 * 验证：通过下标修改数组元素
 * 预期：退出码 0
 */

int main(void) {
    int arr[5];
    int i;

    for (i = 0; i < 5; i = i + 1)
        arr[i] = i * 10;

    /* 修改某些元素 */
    arr[0] = 99;
    arr[3] = 77;

    if (arr[0] != 99) return 1;
    if (arr[1] != 10) return 2;
    if (arr[2] != 20) return 3;
    if (arr[3] != 77) return 4;
    if (arr[4] != 40) return 5;

    /* 通过变量下标赋值 */
    int j = 2;
    arr[j] = 55;
    if (arr[2] != 55) return 6;

    return 0;
}
