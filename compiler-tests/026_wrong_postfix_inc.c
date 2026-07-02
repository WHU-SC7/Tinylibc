/*
 * 026_wrong_postfix_inc.c — 后置自增 x++（已知 bug）
 *
 * 验证：x++ 应返回原值
 * 已知 bug：tcc 返回递增后的新值（和 ++x 一样）
 * 预期：此测试在 tcc 上 FAIL（退出码非 0）
 */

int main(void) {
    int x = 5;
    int a;

    /* x++ 基本 */
    a = x++;
    /* 标准 C: a==5, x==6 */
    /* tcc bug: a==6, x==6 */
    if (a != 5) return 1;  /* tcc 上会触发 */
    if (x != 6) return 2;

    /* x-- */
    x = 10;
    a = x--;
    if (a != 10) return 3;  /* tcc 上可能触发 */
    if (x != 9)  return 4;

    /* 没有后续使用的 x++（仅副作用，可能正确） */
    x = 5;
    x++;
    if (x != 6) return 5;

    return 0;
}
