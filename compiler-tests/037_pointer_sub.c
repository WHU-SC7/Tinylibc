/*
 * 037_pointer_sub.c — 指针减法
 *
 * 验证：指针相减得到元素个数
 * 预期：退出码 0
 */

int main(void) {
    int arr[5];
    int *p, *q;

    p = &arr[0];
    q = &arr[4];

    int diff = q - p;
    if (diff != 4) return 1;  /* 4 个元素的间隔 */

    diff = p - q;
    if (diff != -4) return 2;

    /* char 指针减法 */
    char c_arr[10];
    char *cp = &c_arr[0];
    char *cq = &c_arr[7];
    if (cq - cp != 7) return 3;

    return 0;
}
