/*
 * 020_wrong_pointer_arith.c — 指针算术（已知 bug）
 *
 * 验证：指针加法偏移
 * 已知 bug：tcc 使用 32 位 add 做指针运算，>4GB 地址时截断 segfault
 * 预期：此测试在 tcc 上 SEGFAULT
 */

int main(void) {
    int arr[4];
    int *p;

    arr[0] = 10;
    p = arr;
    /* 32-bit add 截断高 32 位 → segfault */
    if (*(p + 0) != 10) return 1;
    return 0;
}
