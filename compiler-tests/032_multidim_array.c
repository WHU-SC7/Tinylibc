/*
 * 032_multidim_array.c — 多维数组声明与访问
 *
 * 验证：二维数组的声明和下标访问
 * 预期：退出码 0
 */

int main(void) {
    int arr[3][4];
    int i, j;

    /* 填充 */
    for (i = 0; i < 3; i = i + 1)
        for (j = 0; j < 4; j = j + 1)
            arr[i][j] = i * 10 + j;

    /* 验证 */
    if (arr[0][0] != 0)  return 1;
    if (arr[0][3] != 3)  return 2;
    if (arr[1][2] != 12) return 3;
    if (arr[2][1] != 21) return 4;
    if (arr[2][3] != 23) return 5;

    /* 数组元素赋值 */
    arr[1][1] = 99;
    if (arr[1][1] != 99) return 6;

    return 0;
}
