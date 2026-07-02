/*
 * 075_wrong_many_locals.c — 函数 >127 字节局部变量（已知 bug）
 *
 * 验证：大帧函数的 disp8 偏移量
 * 已知 bug：tcc 所有 [rbp+offset] 使用 disp8（截断到 8 位有符号）
 *   → 超过 127 字节时生成错误编码
 * 预期：此测试在 tcc 上 FAIL（编译崩溃或输出错误）
 */

int main(void) {
    /* 每 10 个局部变量占 40 字节；用 40 个 → 160 字节 → 超过 127 */
    int a0, a1, a2, a3, a4, a5, a6, a7, a8, a9;
    int b0, b1, b2, b3, b4, b5, b6, b7, b8, b9;
    int c0, c1, c2, c3, c4, c5, c6, c7, c8, c9;
    int d0, d1, d2, d3, d4, d5, d6, d7, d8, d9;

    a0 = 0;  a1 = 1;  a2 = 2;  a3 = 3;  a4 = 4;
    a5 = 5;  a6 = 6;  a7 = 7;  a8 = 8;  a9 = 9;
    b0 = 10; b1 = 11; b2 = 12; b3 = 13; b4 = 14;
    b5 = 15; b6 = 16; b7 = 17; b8 = 18; b9 = 19;

    /* 验证读取：如果 disp8 溢出，这些会读到错误值 */
    if (a0 != 0)  return 1;
    if (a9 != 9)  return 2;
    if (b0 != 10) return 3;
    if (b9 != 19) return 4;

    (void)c0; (void)c1; (void)c2; (void)c3; (void)c4;
    (void)d0; (void)d1; (void)d2; (void)d3; (void)d4;

    return 0;
}
