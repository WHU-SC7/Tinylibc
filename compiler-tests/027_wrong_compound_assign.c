/*
 * 027_wrong_compound_assign.c — 复合赋值 += -= *=（已知 bug）
 *
 * 验证：x += 3 应等价于 x = x + 3
 * 已知 bug：tcc 生成 x = 3（直接存储右值）
 * 预期：此测试在 tcc 上 FAIL
 */

int main(void) {
    int x;

    /* += */
    x = 5;
    x += 3;
    /* 标准 C: x == 8 */
    /* tcc bug: x == 3 */
    if (x != 8) return 1;

    /* -= */
    x = 10;
    x -= 4;
    if (x != 6) return 2;

    /* *= */
    x = 3;
    x *= 7;
    if (x != 21) return 3;

    /* /= */
    x = 15;
    x /= 4;
    if (x != 3) return 4;  /* 整数除法 */

    /* %= */
    x = 15;
    x %= 4;
    if (x != 3) return 5;

    return 0;
}
